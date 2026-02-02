/**
 * @file gb_input.h
 * @brief Game Boy Input Adapter Header
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __GB_INPUT_H__
#define __GB_INPUT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize input
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_input_init(void);

/**
 * @brief Deinitialize input
 */
void gb_input_deinit(void);

/**
 * @brief Poll input (call from main loop)
 */
void gb_input_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __GB_INPUT_H__ */
