/**
 * @file gb_audio.h
 * @brief Game Boy Audio Adapter Header
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __GB_AUDIO_H__
#define __GB_AUDIO_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize audio
 * @return OPERATE_RET_OK on success
 */
OPERATE_RET gb_audio_init(void);

/**
 * @brief Deinitialize audio
 */
void gb_audio_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __GB_AUDIO_H__ */
