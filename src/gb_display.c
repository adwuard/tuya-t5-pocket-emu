/**
 * @file gb_display.c
 * @brief Game Boy Display Adapter using LVGL
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
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

// Border configuration
#define DISPLAY_BORDER_WIDTH 3 // Border is 3px wide
#define DISPLAY_BORDER_GAP   2 // 2px gap between display and border

lv_obj_t   *gb_canvas           = NULL; // Made non-static so browser can hide/show it
lv_obj_t   *gb_container        = NULL; // Container with border for the canvas
lv_color_t *canvas_buffer       = NULL; // Made non-static so emulator can recreate canvas
static bool display_initialized = false;

// Display thread and semaphore for async display updates
static THREAD_HANDLE display_thread         = NULL;
static SEM_HANDLE    display_sem            = NULL;
static bool          display_thread_running = false;

// Forward declaration
static void display_thread_func(void *arg);

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

    // Create container with border for the canvas
    gb_container = lv_obj_create(scr);
    if (gb_container == NULL) {
        PR_ERR("Failed to create container");
        tal_free(canvas_buffer);
        return OPRT_COM_ERROR;
    }

    // Set container size: canvas size + 2 * gap + 2 * border width
    lv_obj_set_size(gb_container, GB_WIDTH + (DISPLAY_BORDER_GAP * 2) + (DISPLAY_BORDER_WIDTH * 2),
                    GB_HEIGHT + (DISPLAY_BORDER_GAP * 2) + (DISPLAY_BORDER_WIDTH * 2));

    // Set border style: 3px border, black color
    lv_obj_set_style_border_width(gb_container, DISPLAY_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(gb_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gb_container, LV_OPA_TRANSP, 0);       // Transparent background
    lv_obj_set_style_pad_all(gb_container, DISPLAY_BORDER_GAP, 0); // 2px padding creates gap between display and border

    // Center container on screen
    lv_obj_align(gb_container, LV_ALIGN_CENTER, 0, 0);

    // Create LVGL canvas inside the container
    gb_canvas = lv_canvas_create(gb_container);
    if (gb_canvas == NULL) {
        PR_ERR("Failed to create canvas");
        lv_obj_del(gb_container);
        tal_free(canvas_buffer);
        return OPRT_COM_ERROR;
    }

    // Set canvas buffer with RGB565 format
    // LVGL v9 uses LV_COLOR_FORMAT_RGB565 instead of LV_IMG_CF_RGB565
    lv_canvas_set_buffer(gb_canvas, canvas_buffer, GB_WIDTH, GB_HEIGHT, LV_COLOR_FORMAT_RGB565);

    // Set canvas size
    lv_obj_set_size(gb_canvas, GB_WIDTH, GB_HEIGHT);

    // Align canvas to center of container (border will be visible around it)
    lv_obj_align(gb_canvas, LV_ALIGN_CENTER, 0, 0);

    // Create semaphore for display updates (initial count 0, max count 1)
    OPERATE_RET ret = tal_semaphore_create_init(&display_sem, 0, 1);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to create display semaphore: %d", ret);
        lv_obj_del(gb_canvas);
        lv_obj_del(gb_container);
        tal_free(canvas_buffer);
        canvas_buffer = NULL;
        return ret;
    }

    // Create display thread
    display_thread_running  = true;
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth   = 1024 * 8;      // 8KB stack for display thread (LVGL needs more stack)
    thrd_param.priority     = THREAD_PRIO_2; // Lower priority than main emulator thread
    thrd_param.thrdname     = "gb_display";

    ret = tal_thread_create_and_start(&display_thread, NULL, NULL, display_thread_func, NULL, &thrd_param);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to create display thread: %d", ret);
        tal_semaphore_release(display_sem);
        display_sem = NULL;
        lv_obj_del(gb_canvas);
        lv_obj_del(gb_container);
        tal_free(canvas_buffer);
        canvas_buffer = NULL;
        return ret;
    }

    display_initialized = true;
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

    // Stop display thread
    if (display_thread_running) {
        display_thread_running = false;
        // Signal semaphore to wake up thread so it can exit
        if (display_sem != NULL) {
            tal_semaphore_post(display_sem);
        }
        // Wait for thread to exit
        if (display_thread != NULL) {
            tal_thread_delete(display_thread);
            display_thread = NULL;
        }
    }

    // Release semaphore
    if (display_sem != NULL) {
        tal_semaphore_release(display_sem);
        display_sem = NULL;
    }

    if (gb_canvas) {
        lv_obj_del(gb_canvas);
        gb_canvas = NULL;
    }

    if (gb_container) {
        lv_obj_del(gb_container);
        gb_container = NULL;
    }

    if (canvas_buffer) {
        tal_free(canvas_buffer);
        canvas_buffer = NULL;
    }

    display_initialized = false;
}

/**
 * @brief Update display with new frame (internal, called from display thread)
 */
static void gb_display_update_internal(void)
{
    if (!display_initialized || !gb_canvas || !canvas_buffer) {
        return;
    }

    // Don't update display if browser is active (browser has its own screen)
    extern bool gb_browser_is_active(void);
    if (gb_browser_is_active()) {
        return;
    }

    // Use lv_vendor_disp_lock/unlock for thread safety (like tuya_t5_pocket_ai)
    lv_vendor_disp_lock();

    // Make sure container and canvas are visible (not hidden by browser UI)
    if (gb_container) {
        lv_obj_clear_flag(gb_container, LV_OBJ_FLAG_HIDDEN);
    }
    if (gb_canvas) {
        lv_obj_clear_flag(gb_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    // Ensure container is on the screen (might have been removed by browser)
    lv_obj_t *scr = lv_scr_act();
    if (scr != NULL && gb_container && lv_obj_get_parent(gb_container) != scr) {
        lv_obj_set_parent(gb_container, scr);
        lv_obj_align(gb_container, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(gb_container, GB_WIDTH + (DISPLAY_BORDER_GAP * 2) + (DISPLAY_BORDER_WIDTH * 2),
                        GB_HEIGHT + (DISPLAY_BORDER_GAP * 2) + (DISPLAY_BORDER_WIDTH * 2));
    }

    // The framebuffer (fb.ptr) should point to canvas_buffer
    // gnuboy writes RGB565 data directly to fb.ptr during frame rendering
    // We need to ensure the canvas sees this data and invalidate it for redraw

    // Verify framebuffer pointer is correct
    if (fb.ptr != (byte *)canvas_buffer) {
        fb.ptr = (byte *)canvas_buffer;
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
 * @brief Update display with new frame (public API - signals display thread)
 */
void gb_display_update(void)
{
    // Signal the display thread to update (non-blocking)
    if (display_sem != NULL) {
        tal_semaphore_post(display_sem);
    }
}

/**
 * @brief Display thread function - waits for semaphore and updates display
 */
static void display_thread_func(void *arg)
{
    (void)arg;

    while (display_thread_running) {
        // Wait for display update signal (blocking wait)
        OPERATE_RET ret = tal_semaphore_wait(display_sem, SEM_WAIT_FOREVER);
        if (ret != OPRT_OK) {
            PR_ERR("Display thread: semaphore wait failed: %d", ret);
            continue;
        }

        // Check if we should still be running
        if (!display_thread_running) {
            break;
        }

        // Update the display
        gb_display_update_internal();
    }
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
    }
}

void vid_end(void)
{
    // End frame rendering - signal display thread to update (non-blocking)
    // The display thread will handle the actual LVGL update asynchronously
    // This prevents blocking the main emulator loop
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
}
