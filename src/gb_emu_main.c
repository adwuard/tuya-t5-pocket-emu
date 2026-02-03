/**
 * @file gb_emu_main.c
 * @brief Game Boy Emulator Main Entry Point for Tuya T5 Pocket
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "board_com_api.h"
#include "lv_vendor.h" // For lv_vendor_disp_lock/unlock
#include "gb_emu.h"
#include "gb_display.h"
#include "gb_input.h"
#include "gb_audio.h"
#include "gb_storage.h"
#include "gb_browser.h"
#include "gb_save.h"

#include "emu.h"
#include "loader.h"
#include "defs.h"
#include "hw.h"
#include "sys.h"
#include "regs.h"
#include "cpu.h"
#include "lcd.h"
#include "rtc.h"
#include "sound.h"
#include "fb.h"         // For struct fb
#include "gb_display.h" // For GB_WIDTH, GB_HEIGHT, and gb_canvas
#include "lvgl.h"       // For LVGL functions
#include <string.h>
#include <stdlib.h>

// Forward declaration (emu_step is in emu.c but not in emu.h)
void emu_step(void);

// Game Boy screen dimensions (from gb_display.c)
#define GB_WIDTH  160
#define GB_HEIGHT 144

static bool  gb_emu_running   = false;
static char *current_rom_path = NULL;
// static bool  frame_initialized = false; // Track if vid_begin/lcd_begin have been called

/**
 * @brief Initialize Game Boy emulator
 */
OPERATE_RET gb_emu_init(void)
{
    OPERATE_RET ret = OPRT_OK;

    PR_NOTICE("Initializing Game Boy Emulator...");

    // Initialize display
    ret = gb_display_init();
    if (ret != OPRT_OK) {
        PR_ERR("Display initialization failed: %d", ret);
        return ret;
    }

    // Initialize input
    ret = gb_input_init();
    if (ret != OPRT_OK) {
        PR_ERR("Input initialization failed: %d", ret);
        return ret;
    }

    // Initialize audio
    ret = gb_audio_init();
    if (ret != OPRT_OK) {
        PR_ERR("Audio initialization failed: %d", ret);
        return ret;
    }

    // Initialize storage
    ret = gb_storage_init();
    if (ret != OPRT_OK) {
        PR_ERR("Storage initialization failed: %d", ret);
        return ret;
    }

    // Initialize gnuboy system (following SDL2-GNUBoy sequence)
    // Note: vid_preinit() is called early (before video init)
    vid_preinit();

    // Note: vid_init() and pcm_init() are called here, but ROM loading
    // and emu_reset() happen later in gb_emu_load_rom()
    vid_init();
    pcm_init();

    // Initialize I/O (like SDL2-GNUBoy's emu_init())
    // emu_init();

    PR_NOTICE("Game Boy Emulator initialized successfully");

    return OPRT_OK;
}

/**
 * @brief Load ROM file
 */
OPERATE_RET gb_emu_load_rom(const char *rom_path)
{
    if (rom_path == NULL) {
        PR_ERR("ROM path is NULL");
        return OPRT_INVALID_PARM;
    }

    PR_NOTICE("Loading ROM: %s", rom_path);

    // Initialize save state system
    OPERATE_RET ret = gb_save_init(rom_path);
    if (ret != OPRT_OK) {
        PR_WARN("Save state init failed, continuing anyway");
    }

    // Free previous ROM path if exists
    if (current_rom_path) {
        tal_free(current_rom_path);
        current_rom_path = NULL;
    }

    // Set savedir for gnuboy (must be set before loader_init)
    // Note: loader.c uses static savedir variable
    // We need to set it via a function or modify loader.c
    // For now, we'll set it in gb_save_init and loader_init will use it

    // Load ROM using gnuboy loader (following SDL2-GNUBoy sequence)
    // SDL2 sequence: vid_init() -> pcm_init() -> loader_init() -> emu_reset() -> emu_run()
    loader_init((char *)rom_path);
    // Note: loader_init doesn't return error, so we assume success

    // Reset emulator state after loading ROM (required by gnuboy)
    // This initializes CPU, LCD, MBC, sound, I/O, and memory mapping
    // Following SDL2-GNUBoy: emu_reset() is called AFTER loader_init()
    emu_reset();

    // Browser is disabled, so canvas should already exist from gb_display_init()
    // Just make sure it's visible
    extern lv_obj_t *gb_canvas;
    extern lv_obj_t *gb_container;
    if (gb_container != NULL && gb_canvas != NULL) {
        lv_vendor_disp_lock();
        lv_obj_clear_flag(gb_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(gb_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(gb_container); // Bring container (and canvas) to front
        // Note: LVGL runs in its own thread, so we don't need to call lv_task_handler() here
        // Just unlock and let LVGL thread handle the rendering
        lv_vendor_disp_unlock();
        PR_NOTICE("Canvas and container made visible");
    } else {
        PR_ERR("Canvas or container is NULL - display may not be initialized");
    }

    // Save ROM path
    current_rom_path = (char *)tal_malloc(strlen(rom_path) + 1);
    if (current_rom_path == NULL) {
        PR_ERR("Failed to allocate memory for ROM path");
        return OPRT_MALLOC_FAILED;
    }
    strcpy(current_rom_path, rom_path);

    // Mark emulator as running now that ROM is loaded
    gb_emu_running = true;

    PR_NOTICE("ROM loaded successfully");

    return OPRT_OK;
}

/**
 * @brief Run one frame of emulation (non-blocking)
 * This should be called repeatedly from the main loop
 *
 * Extracted from SDL2-GNUBoy's emu_run() to run one frame at a time
 * Optimized for faster loop execution without changing CPU cycles
 */
void gb_emu_run(void)
{
    // Cache register values to avoid repeated macro/register access
    register byte ly;
    register byte lcdc;

    // Start frame: emulate until we reach visible scanlines
    cpu_emulate(2280);

    // Draw visible scanlines (0-143)
    // Cache R_LY in register variable for faster access
    ly = R_LY;
    while (ly > 0 && ly < 144) {
        emu_step();
        ly = R_LY; // Update cached value
    }

    // End of frame - update display, sound, RTC
    vid_end();
    rtc_tick();
    sound_mix();
    pcm_submit();

    // Poll input and process events (equivalent to doevents() in SDL2-GNUBoy)
    // doevents() calls ev_poll() to post events, then processes them via ev_getevent()
    doevents();

    // Begin next frame
    vid_begin();

    // Handle LCD disabled case (LCD off)
    // Cache R_LCDC to avoid repeated access
    lcdc = R_LCDC;
    if (!(lcdc & 0x80)) {
        cpu_emulate(32832);
    }

    // Wait for next frame to start (LY resets to 0 at frame start)
    // This loop runs until we're at the start of the next frame
    // Cache R_LY in register variable for faster access
    ly = R_LY;
    while (ly > 0) {
        emu_step();
        ly = R_LY; // Update cached value
    }
}

/**
 * @brief Stop emulator
 */
void gb_emu_stop(void)
{
    PR_NOTICE("Stopping Game Boy emulator...");
    gb_emu_running = false;
}

/**
 * @brief Reset emulator
 */
void gb_emu_reset(void)
{
    PR_NOTICE("Resetting Game Boy emulator...");
    emu_reset();
}

/**
 * @brief Deinitialize emulator
 */
void gb_emu_deinit(void)
{
    PR_NOTICE("Deinitializing Game Boy emulator...");

    gb_emu_running = false;

    // Close gnuboy systems
    pcm_close();
    vid_close();

    // Deinitialize adapters
    gb_audio_deinit();
    gb_input_deinit();
    gb_display_deinit();
    gb_storage_deinit();

    // Free ROM path
    if (current_rom_path) {
        tal_free(current_rom_path);
        current_rom_path = NULL;
    }

    PR_NOTICE("Game Boy emulator deinitialized");
}

/**
 * @brief Get current ROM path
 */
const char *gb_emu_get_rom_path(void)
{
    return current_rom_path;
}

/**
 * @brief Check if emulator is running
 */
bool gb_emu_is_running(void)
{
    return gb_emu_running;
}

/**
 * @brief Save emulator state to slot
 */
OPERATE_RET gb_emu_save_state(int slot)
{
    return gb_save_state(slot);
}

/**
 * @brief Load emulator state from slot
 */
OPERATE_RET gb_emu_load_state(int slot)
{
    return gb_load_state(slot);
}
