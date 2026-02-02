/**
 * @file gb_storage.h
 * @brief Game Boy Storage Adapter Header
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __GB_STORAGE_H__
#define __GB_STORAGE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize storage
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_storage_init(void);

/**
 * @brief Deinitialize storage
 */
void gb_storage_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __GB_STORAGE_H__ */
