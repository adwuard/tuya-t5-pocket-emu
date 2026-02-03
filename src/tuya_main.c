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
#include "gb_browser.h"
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

    // Mount SD card first (required before loading ROM)
    // Configure SDIO pins (required before mounting)
    tkl_io_pinmux_config(TUYA_GPIO_NUM_14, TUYA_SDIO_HOST_CLK);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_15, TUYA_SDIO_HOST_CMD);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_16, TUYA_SDIO_HOST_D0);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_17, TUYA_SDIO_HOST_D1);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_18, TUYA_SDIO_HOST_D2);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_19, TUYA_SDIO_HOST_D3);

    ret = tkl_fs_mount("/sdcard", DEV_SDCARD);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to mount SD card: %d", ret);
        PR_ERR("Cannot load ROM without SD card");
        return;
    }

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

    // Initialize browser
    ret = gb_browser_init();
    if (ret != OPRT_OK) {
        PR_ERR("Browser initialization failed: %d", ret);
        return;
    }

    // Show ROM browser first (user selects ROM)
    gb_browser_show();

    // Main loop
    while (1) {
        // Poll input first (updates keystates for both browser and emulator)
        gb_input_poll();

        // Handle browser first (takes control of input before emulator)
        if (gb_browser_is_active()) {
            // Check if user selected a ROM (check this first, before browser input)
            char *selected_rom = gb_browser_get_selected();
            if (selected_rom) {
                // Clean up browser UI properly before loading ROM
                extern void gb_browser_cleanup_for_emu(void);
                gb_browser_cleanup_for_emu();

                // Additional delay to ensure UI cleanup is complete
                tal_system_sleep(100);

                ret = gb_emu_load_rom(selected_rom);
                if (ret != OPRT_OK) {
                    PR_ERR("Failed to load ROM: %d", ret);
                    // Show browser again if ROM load failed
                    gb_browser_show();
                }

                tal_free(selected_rom);
                // Skip browser input handling this iteration since ROM was selected
                // Continue to emulator run
            } else {
                // Handle browser input if no ROM selected
                gb_browser_handle_input();
                gb_browser_update();
            }
        }

        // Run emulator if ROM is loaded (runs one frame per loop iteration)
        // Only run if browser is not active (browser takes priority)
        if (gb_emu_is_running() && !gb_browser_is_active()) {
            gb_emu_run(); // Run one frame, then return to main loop
        } else if (!gb_browser_is_active() && !gb_emu_is_running()) {
            // If no ROM loaded and browser not active, show browser
            gb_browser_show();
        }

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
    thrd_param.stackDepth   = 1024 * 8; // 8KB stack (ROM loading needs more stack)
    thrd_param.priority     = THREAD_PRIO_1;
    thrd_param.thrdname     = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
