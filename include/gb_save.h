/**
 * @file gb_save.h
 * @brief Game Boy Save State Management
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef GB_SAVE_H
#define GB_SAVE_H

#include "tuya_cloud_types.h"

/**
 * @brief Initialize save state system
 */
OPERATE_RET gb_save_init(const char *rom_path);

/**
 * @brief Save emulator state to slot
 * @param slot Save slot (0-9), or -1 for default slot
 */
OPERATE_RET gb_save_state(int slot);

/**
 * @brief Load emulator state from slot
 * @param slot Save slot (0-9), or -1 for default slot
 */
OPERATE_RET gb_load_state(int slot);

/**
 * @brief Save SRAM (battery-backed RAM)
 */
OPERATE_RET gb_save_sram(void);

/**
 * @brief Load SRAM (battery-backed RAM)
 */
OPERATE_RET gb_load_sram(void);

#endif // GB_SAVE_H
