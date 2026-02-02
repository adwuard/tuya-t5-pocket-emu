/**
 * @file tuya_main.c
 * @brief Main entry point for Game Boy Emulator App
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"
#include "board_com_api.h"
#include "tkl_fs.h"     // For SD card mounting (DEV_SDCARD)
#include "tkl_pinmux.h" // For SDIO pin configuration
#include "lv_vendor.h"
#include "gb_emu.h"
// #include "gb_browser.h" // Browser disabled - auto-load ROM instead
#include "gb_input.h"

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/**
 * @brief User-defined log output callback
 */
void user_log_output_cb(const char *str)
{
    tal_uart_write(TUYA_UART_NUM_0, (const uint8_t *)str, strlen(str));
}

/**
 * @brief User main function
 */
void user_main(void)
{
    OPERATE_RET ret = OPRT_OK;

    // Initialize logging
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("========================================");
    PR_NOTICE("Game Boy Emulator for Tuya T5 Pocket");
    PR_NOTICE("Version: %s", PROJECT_VERSION);
    PR_NOTICE("========================================");

    // Initialize Tuya systems
    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key  = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init();
    tal_time_service_init();

    // Register hardware
    ret = board_register_hardware();
    if (ret != OPRT_OK) {
        PR_ERR("board_register_hardware failed: %d", ret);
        return;
    }

    // Initialize LVGL FIRST (before SD card mounting)
    // This ensures display is ready before any UI operations
    PR_NOTICE("Initializing LVGL...");
#ifdef DISPLAY_NAME
    lv_vendor_init(DISPLAY_NAME);
#else
    lv_vendor_init("st7305");
#endif
    lv_vendor_start(5, 1024 * 8);
    tal_system_sleep(100); // Allow LVGL to initialize

    // Initialize Game Boy emulator (this also initializes display)
    ret = gb_emu_init();
    if (ret != OPRT_OK) {
        PR_ERR("gb_emu_init failed: %d", ret);
        return;
    }

    PR_NOTICE("Game Boy Emulator ready");

    // Mount SD card first (required before loading ROM)
    PR_NOTICE("Mounting SD card...");

    // Configure SDIO pins (required before mounting)
    tkl_io_pinmux_config(TUYA_GPIO_NUM_14, TUYA_SDIO_HOST_CLK);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_15, TUYA_SDIO_HOST_CMD);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_16, TUYA_SDIO_HOST_D0);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_17, TUYA_SDIO_HOST_D1);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_18, TUYA_SDIO_HOST_D2);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_19, TUYA_SDIO_HOST_D3);
    PR_NOTICE("SDIO pins configured");

    ret = tkl_fs_mount("/sdcard", DEV_SDCARD);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to mount SD card: %d", ret);
        PR_ERR("Cannot load ROM without SD card");
        return;
    }
    PR_NOTICE("SD card mounted successfully");

    // DIR *roms_dir = opendir("/sdcard/roms");
    // if (roms_dir) {
    //     struct dirent *entry;
    //     PR_NOTICE("Listing /sdcard/roms:");
    //     while ((entry = readdir(roms_dir)) != NULL) {
    //         PR_NOTICE("  %s", entry->d_name);
    //     }
    //     closedir(roms_dir);
    // } else {
    //     PR_ERR("Failed to open /sdcard/roms for reading");
    // }

    // Wait a bit for SD card to stabilize
    tal_system_sleep(200);

    // Auto-load ROM on boot
    const char *rom_path = "/sdcard/roms/Mega_Man_V.gb";
    PR_NOTICE("Auto-loading ROM: %s", rom_path);
    ret = gb_emu_load_rom(rom_path);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to auto-load ROM: %d", ret);
    } else {
        PR_NOTICE("ROM loaded successfully, waiting 5 seconds before starting emulator...");
        // Wait 5 seconds to allow test pattern to be visible
        tal_system_sleep(5000);
        PR_NOTICE("Starting emulator...");
    }

    // Show ROM browser (disabled)
    // gb_browser_show();

    // Main loop
    while (1) {
        // Poll input
        // gb_input_poll();

        // Browser disabled - ROM is auto-loaded on boot
        // Check if user selected a ROM (check this first, before browser input)
        // if (gb_browser_is_active()) {
        //     char *selected_rom = gb_browser_get_selected();
        //     if (selected_rom) {
        //         PR_NOTICE("ROM selected: %s", selected_rom);
        //         ret = gb_emu_load_rom(selected_rom);
        //         if (ret != OPRT_OK) {
        //             PR_ERR("Failed to load ROM: %d", ret);
        //         } else {
        //             // Hide browser UI after successful ROM load
        //             // The browser will be deactivated by gb_browser_get_selected()
        //             PR_NOTICE("Starting emulator...");
        //         }
        //
        //         tal_free(selected_rom);
        //         // Skip browser input handling this iteration since ROM was selected
        //         // Continue to emulator run
        //     } else {
        //         // Handle browser input if no ROM selected
        //         gb_browser_handle_input();
        //         gb_browser_update();
        //     }
        // }

        // Run emulator if ROM is loaded (runs one frame per loop iteration)
        // if (gb_emu_is_running()) {
        //     static bool first_run = true;
        //     if (first_run) {
        //         PR_NOTICE("Starting emulator main loop...");
        //         first_run = false;
        //     }
        gb_emu_run(); // Run one frame, then return to main loop
        // }
        // Browser disabled - no need to show browser if ROM not loaded
        // else if (!gb_browser_is_active()) {
        //     // If no ROM loaded and browser not active, show browser
        //     gb_browser_show();
        // }

        // Note: LVGL runs in its own thread (created by lv_vendor_start())
        // We should NOT call lv_task_handler() manually - it's handled by the LVGL thread
        // We only need to use lv_vendor_disp_lock/unlock when accessing LVGL from this thread

        // Small delay
        // tal_system_sleep(10);
    }
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#else

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief  task thread
 *
 * @param[in] arg:Parameters when creating a task
 * @return none
 */
static void tuya_app_thread(void *arg)
{
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth   = 1024 * 4;
    thrd_param.priority     = THREAD_PRIO_1;
    thrd_param.thrdname     = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
