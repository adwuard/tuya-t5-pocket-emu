/**
 * @file gb_save.c
 * @brief Game Boy Save State Management
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_fs.h"
#include "gb_save.h"

#include "loader.h"
#include "save.h"
#include "defs.h"
#include "mem.h"
#include "lcd.h"
#include "sound.h"
#include "rtc.h"
#include <string.h>
#include <stdlib.h>

#define SAVE_DIR_PATH "/sdcard/saves/gb"
#define MAX_SAVE_PATH 256

// External reference to savedir in loader.c
// We need to set this before loader_init is called
extern char *savedir;

/**
 * @brief Initialize save state system
 */
OPERATE_RET gb_save_init(const char *rom_path)
{
    if (rom_path == NULL) {
        return OPRT_INVALID_PARM;
    }

    // Create save directory if it doesn't exist
    tkl_fs_mkdir(SAVE_DIR_PATH);

    // Set savedir for gnuboy loader (must be set before loader_init)
    // Free previous savedir if it exists
    if (savedir) {
        free(savedir);
        savedir = NULL;
    }

    // Allocate and set savedir
    savedir = (char *)malloc(strlen(SAVE_DIR_PATH) + 1);
    if (savedir == NULL) {
        PR_ERR("Failed to allocate memory for savedir");
        return OPRT_MALLOC_FAILED;
    }
    strcpy(savedir, SAVE_DIR_PATH);
    return OPRT_OK;
}

/**
 * @brief Save emulator state to slot
 */
OPERATE_RET gb_save_state(int slot)
{
    if (slot < -1 || slot > 9) {
        PR_ERR("Invalid save slot: %d", slot);
        return OPRT_INVALID_PARM;
    }

    // Use gnuboy's state_save function
    // It will use saveprefix set by loader_init
    state_save(slot);
    return OPRT_OK;
}

/**
 * @brief Load emulator state from slot
 */
OPERATE_RET gb_load_state(int slot)
{
    if (slot < -1 || slot > 9) {
        PR_ERR("Invalid save slot: %d", slot);
        return OPRT_INVALID_PARM;
    }

    // Use gnuboy's state_load function
    state_load(slot);
    return OPRT_OK;
}

/**
 * @brief Save SRAM (battery-backed RAM)
 */
OPERATE_RET gb_save_sram(void)
{
    // Use gnuboy's sram_save function
    int ret = sram_save();
    if (ret == 0) {
        return OPRT_OK;
    } else {
        PR_ERR("SRAM save failed");
        return OPRT_COM_ERROR;
    }
}

/**
 * @brief Load SRAM (battery-backed RAM)
 */
OPERATE_RET gb_load_sram(void)
{
    // Use gnuboy's sram_load function
    int ret = sram_load();
    if (ret == 0) {
        return OPRT_OK;
    } else {
        PR_ERR("SRAM load failed (may not exist)");
        return OPRT_COM_ERROR;
    }
}
