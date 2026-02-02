/**
 * @file gb_emu.h
 * @brief Game Boy Emulator API Header
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __GB_EMU_H__
#define __GB_EMU_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Game Boy emulator
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_emu_init(void);

/**
 * @brief Load ROM file
 * @param rom_path Path to ROM file
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_emu_load_rom(const char *rom_path);

/**
 * @brief Start emulator main loop
 */
void gb_emu_run(void);

/**
 * @brief Stop emulator
 */
void gb_emu_stop(void);

/**
 * @brief Reset emulator
 */
void gb_emu_reset(void);

/**
 * @brief Deinitialize emulator
 */
void gb_emu_deinit(void);

/**
 * @brief Get current ROM path
 * @return ROM path or NULL
 */
const char *gb_emu_get_rom_path(void);

/**
 * @brief Check if emulator is running
 * @return true if running
 */
bool gb_emu_is_running(void);

/**
 * @brief Save emulator state to file
 * @param slot Save slot number (0-9), or -1 for default slot
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_emu_save_state(int slot);

/**
 * @brief Load emulator state from file
 * @param slot Save slot number (0-9), or -1 for default slot
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_emu_load_state(int slot);

#ifdef __cplusplus
}
#endif

#endif /* __GB_EMU_H__ */
