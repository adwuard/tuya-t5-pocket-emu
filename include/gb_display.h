/**
 * @file gb_display.h
 * @brief Game Boy Display Adapter Header
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __GB_DISPLAY_H__
#define __GB_DISPLAY_H__

#include "tuya_cloud_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize display
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_display_init(void);

/**
 * @brief Deinitialize display
 */
void gb_display_deinit(void);

/**
 * @brief Update display with new frame
 */
void gb_display_update(void);

/**
 * @brief Get framebuffer pointer
 * @return Pointer to framebuffer (RGB565 format, 160x144)
 */
uint16_t *gb_display_get_framebuffer(void);

#ifdef __cplusplus
}
#endif

#endif /* __GB_DISPLAY_H__ */
