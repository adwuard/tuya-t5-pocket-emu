/**
 * @file gb_browser.h
 * @brief SD Card ROM Browser with Input Navigation
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef GB_BROWSER_H
#define GB_BROWSER_H

#include "tuya_cloud_types.h"

/**
 * @brief Initialize SD card and browser
 */
OPERATE_RET gb_browser_init(void);

/**
 * @brief Deinitialize browser
 */
void gb_browser_deinit(void);

/**
 * @brief Show ROM browser UI
 */
void gb_browser_show(void);

/**
 * @brief Get selected ROM path (call after gb_browser_show)
 * @return Selected ROM path (must be freed by caller), or NULL if not selected yet
 */
char *gb_browser_get_selected(void);

/**
 * @brief Update browser (call in main loop when browser is active)
 */
void gb_browser_update(void);

/**
 * @brief Check if browser is active
 */
bool gb_browser_is_active(void);

/**
 * @brief Clean up browser and transition to emulator
 */
void gb_browser_cleanup_for_emu(void);

/**
 * @brief Handle browser input (call from input poll)
 */
void gb_browser_handle_input(void);

#endif // GB_BROWSER_H
