/**
 * @file gb_display.c
 * @brief Game Boy Display Adapter using LVGL
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "lv_vendor.h"
#include "gb_display.h"

#include "sys.h"
#include "defs.h"
#include "lcd.h"
#include "fb.h"
#include "lvgl.h" // For lv_refr_now

// Game Boy screen dimensions
#define GB_WIDTH  160
#define GB_HEIGHT 144

// Display dimensions (T5 Pocket)
#define DISP_WIDTH  168
#define DISP_HEIGHT 384

lv_obj_t   *gb_canvas           = NULL; // Made non-static so browser can hide/show it
lv_color_t *canvas_buffer       = NULL; // Made non-static so emulator can recreate canvas
static bool display_initialized = false;

// GNUBoy framebuffer structure (defined here, declared extern in fb.h)
struct fb fb;

/**
 * @brief Initialize display
 * Note: LVGL should already be initialized by tuya_main.c before calling this
 */
OPERATE_RET gb_display_init(void)
{
    if (display_initialized) {
        return OPRT_OK;
    }

    PR_NOTICE("Initializing GB display...");

    // LVGL should already be initialized, but verify
    lv_obj_t *scr = lv_scr_act();
    if (scr == NULL) {
        PR_ERR("LVGL not initialized - screen object is NULL");
        return OPRT_COM_ERROR;
    }

    // Allocate canvas buffer (RGB565 format)
    canvas_buffer = (lv_color_t *)tal_malloc(GB_WIDTH * GB_HEIGHT * sizeof(lv_color_t));
    if (canvas_buffer == NULL) {
        PR_ERR("Failed to allocate canvas buffer");
        return OPRT_MALLOC_FAILED;
    }

    // Create LVGL canvas
    gb_canvas = lv_canvas_create(scr);
    if (gb_canvas == NULL) {
        PR_ERR("Failed to create canvas");
        tal_free(canvas_buffer);
        return OPRT_COM_ERROR;
    }

    // Set canvas buffer with RGB565 format
    // LVGL v9 uses LV_COLOR_FORMAT_RGB565 instead of LV_IMG_CF_RGB565
    lv_canvas_set_buffer(gb_canvas, canvas_buffer, GB_WIDTH, GB_HEIGHT, LV_COLOR_FORMAT_RGB565);

    // Center canvas on screen
    lv_obj_align(gb_canvas, LV_ALIGN_CENTER, 0, 0);

    // Set canvas size
    lv_obj_set_size(gb_canvas, GB_WIDTH, GB_HEIGHT);

    display_initialized = true;
    PR_NOTICE("GB display initialized");

    return OPRT_OK;
}

/**
 * @brief Deinitialize display
 */
void gb_display_deinit(void)
{
    if (!display_initialized) {
        return;
    }

    if (gb_canvas) {
        lv_obj_del(gb_canvas);
        gb_canvas = NULL;
    }

    if (canvas_buffer) {
        tal_free(canvas_buffer);
        canvas_buffer = NULL;
    }

    display_initialized = false;
    PR_NOTICE("GB display deinitialized");
}

/**
 * @brief Update display with new frame
 */
void gb_display_update(void)
{
    if (!display_initialized || !gb_canvas || !canvas_buffer) {
        return;
    }

    // Use lv_vendor_disp_lock/unlock for thread safety (like tuya_t5_pocket_ai)
    lv_vendor_disp_lock();

    // Make sure canvas is visible (not hidden by browser UI)
    lv_obj_clear_flag(gb_canvas, LV_OBJ_FLAG_HIDDEN);

    // Ensure canvas is on the screen (might have been removed by browser)
    lv_obj_t *scr = lv_scr_act();
    if (scr != NULL && lv_obj_get_parent(gb_canvas) != scr) {
        lv_obj_set_parent(gb_canvas, scr);
        lv_obj_align(gb_canvas, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(gb_canvas, GB_WIDTH, GB_HEIGHT);
    }

    // The framebuffer (fb.ptr) should point to canvas_buffer
    // gnuboy writes RGB565 data directly to fb.ptr during frame rendering
    // We need to ensure the canvas sees this data and invalidate it for redraw

    // Verify framebuffer pointer is correct
    if (fb.ptr != (byte *)canvas_buffer) {
        PR_WARN("gb_display_update: fb.ptr mismatch! fb.ptr=%p, canvas_buffer=%p", fb.ptr, canvas_buffer);
        // Fix it - ensure fb.ptr points to canvas_buffer
        fb.ptr = (byte *)canvas_buffer;
    }

    // Debug: Sample framebuffer data to verify it's being written
    static int update_debug_count = 0;
    if (update_debug_count < 5 && fb.ptr != NULL) {
        uint16_t *fb_data = (uint16_t *)fb.ptr;
        uint16_t  sample[4];
        sample[0] = fb_data[0];
        sample[1] = fb_data[GB_WIDTH * GB_HEIGHT / 2];
        sample[2] = fb_data[GB_WIDTH * GB_HEIGHT - 1];
        sample[3] = fb_data[GB_WIDTH * 72]; // Middle of screen
        PR_NOTICE("gb_display_update[%d]: canvas_buffer samples: 0x%04X 0x%04X 0x%04X 0x%04X", update_debug_count,
                  sample[0], sample[1], sample[2], sample[3]);
        update_debug_count++;
    }

    // The canvas buffer is already set up with lv_canvas_set_buffer
    // gnuboy writes directly to canvas_buffer (via fb.ptr)
    // We just need to invalidate the canvas to trigger LVGL to redraw it
    // LVGL will automatically convert RGB565 to monochrome during rendering

    // Invalidate the entire canvas to force redraw
    lv_obj_invalidate(gb_canvas);

    // Force an immediate refresh to ensure the frame is displayed
    // Note: We're holding the display lock, so this is safe
    lv_refr_now(lv_disp_get_default());

    lv_vendor_disp_unlock();
}

/**
 * @brief Get framebuffer pointer
 */
uint16_t *gb_display_get_framebuffer(void)
{
    return (uint16_t *)fb.ptr;
}

// GNUBoy sys.h interface implementations

void vid_preinit(void)
{
    // Pre-initialization (if needed)
}

void vid_init(void)
{
    // Display is already initialized in gb_display_init()
    // Setup gnuboy framebuffer structure
    // Note: canvas_buffer must be allocated before this is called
    if (canvas_buffer == NULL) {
        PR_ERR("vid_init: canvas_buffer is NULL - display not initialized");
        return;
    }

    fb.w       = GB_WIDTH;
    fb.h       = GB_HEIGHT;
    fb.pelsize = 2; // RGB565 = 2 bytes per pixel
    fb.pitch   = fb.w * fb.pelsize;
    fb.indexed = 0;
    fb.ptr     = (byte *)canvas_buffer; // Use our canvas buffer

    // Set color component bit shifts for RGB565 format
    // RGB565: R=5 bits (shift 11), G=6 bits (shift 5), B=5 bits (shift 0)
    // Note: SDL2 uses BGRA32 with different shifts, but we use RGB565
    fb.cc[0].r = 5;  // Red component: 5 bits, shift right by 11
    fb.cc[0].l = 11; // Red component: shift left by 11
    fb.cc[1].r = 6;  // Green component: 6 bits, shift right by 5
    fb.cc[1].l = 5;  // Green component: shift left by 5
    fb.cc[2].r = 5;  // Blue component: 5 bits, shift right by 0
    fb.cc[2].l = 0;  // Blue component: shift left by 0
    fb.cc[3].r = 0;  // Alpha (not used in RGB565)
    fb.cc[3].l = 0;

    fb.enabled = 1;
    fb.dirty   = 0;

    PR_NOTICE("vid_init: Framebuffer initialized (ptr=%p, size=%dx%d, pelsize=%d, RGB565)", fb.ptr, fb.w, fb.h,
              fb.pelsize);

    // Debug: Fill framebuffer with RGB565 test pattern to verify display and color conversion
    // This will be overwritten by the first frame, but helps verify the display path
    uint16_t *test_fb = (uint16_t *)fb.ptr;

    // Simple black/white checkerboard pattern
    // RGB565: Black = 0x0000, White = 0xFFFF
    for (int y = 0; y < fb.h; y++) {
        for (int x = 0; x < fb.w; x++) {
            // Create checkerboard pattern: alternate black and white every 16 pixels
            uint16_t color        = ((x / 16) + (y / 16)) % 2 ? 0xFFFF : 0x0000;
            test_fb[y * fb.w + x] = color;
        }
    }
    PR_NOTICE("vid_init: Black/white checkerboard test pattern written to framebuffer");

    // Invalidate canvas to display the test pattern
    extern lv_obj_t *gb_canvas;
    if (gb_canvas != NULL) {
        lv_vendor_disp_lock();
        lv_obj_invalidate(gb_canvas);
        lv_refr_now(lv_disp_get_default());
        lv_vendor_disp_unlock();
        PR_NOTICE("vid_init: Test pattern displayed on canvas");
    }
}

void vid_close(void)
{
    gb_display_deinit();
}

void vid_begin(void)
{
    // Begin frame rendering
    // In SDL2, this sets fb.ptr to a temporary buffer (pix)
    // In our case, we set fb.ptr to canvas_buffer so gnuboy writes directly to it
    // Ensure fb.ptr is set correctly (it should already be set in vid_init(), but verify)
    if (fb.ptr == NULL && canvas_buffer != NULL) {
        fb.ptr = (byte *)canvas_buffer;
        PR_WARN("vid_begin: fb.ptr was NULL, resetting to canvas_buffer");
    }
}

void vid_end(void)
{
    // End frame rendering - update display
    // In SDL2, this copies the framebuffer to the texture
    // In our case, gnuboy writes directly to canvas_buffer (via fb.ptr)
    // So we just need to invalidate the canvas to trigger redraw

    // Debug: Check if framebuffer has been written to
    static int frame_debug_count = 0;
    if (frame_debug_count < 5 && fb.ptr != NULL) {
        uint16_t *fb_data = (uint16_t *)fb.ptr;
        uint16_t  sample[4];
        sample[0] = fb_data[0];
        sample[1] = fb_data[GB_WIDTH * GB_HEIGHT / 2];
        sample[2] = fb_data[GB_WIDTH * GB_HEIGHT - 1];
        sample[3] = fb_data[GB_WIDTH * 72]; // Middle of screen
        PR_NOTICE("vid_end[%d]: fb samples: 0x%04X 0x%04X 0x%04X 0x%04X", frame_debug_count, sample[0], sample[1],
                  sample[2], sample[3]);
        frame_debug_count++;
    }

    gb_display_update();
}

void vid_setpal(int i, int r, int g, int b)
{
    // Set palette color (for DMG mode)
    // This is handled by gnuboy's palette system
    // Note: For RGB565, we don't need to set individual palette colors
    // as gnuboy writes RGB565 values directly to the framebuffer
    (void)i;
    (void)r;
    (void)g;
    (void)b;
}

void vid_settitle(char *title)
{
    // Set window title (not applicable for embedded)
}

void vid_fullscreen_toggle(void)
{
    // Not applicable for embedded
}

void vid_screenshot(void)
{
    // TODO: Implement screenshot functionality
    PR_NOTICE("Screenshot requested (not implemented)");
}
