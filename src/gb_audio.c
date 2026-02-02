/**
 * @file gb_audio.c
 * @brief Game Boy Audio Adapter using Tuya Audio
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tdd_audio.h"
#include "tdl_audio_manage.h"
#include "gb_audio.h"

#include "defs.h"
#include "sys.h"
#include "sound.h"
#include "pcm.h"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS    1

static bool               audio_initialized = false;
static TDL_AUDIO_HANDLE_T audio_handle      = NULL;

// GNUBoy PCM structure (defined here, declared extern in pcm.h)
struct pcm pcm;

/**
 * @brief Initialize audio
 */
OPERATE_RET gb_audio_init(void)
{
    OPERATE_RET ret = OPRT_OK;

    if (audio_initialized) {
        return OPRT_OK;
    }

    PR_NOTICE("Initializing GB audio...");

#if defined(AUDIO_CODEC_NAME)
    // Find audio device
    ret = tdl_audio_find(AUDIO_CODEC_NAME, &audio_handle);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to find audio device: %d", ret);
        return ret;
    }

    // Open audio device (no mic callback needed for playback only)
    ret = tdl_audio_open(audio_handle, NULL);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to open audio device: %d", ret);
        return ret;
    }

    // Get audio info to verify configuration
    TDL_AUDIO_INFO_T audio_info;
    ret = tdl_audio_get_info(audio_handle, &audio_info);
    if (ret == OPRT_OK) {
        PR_NOTICE("Audio info: rate=%d, ch=%d, bits=%d", audio_info.sample_rate, audio_info.sample_ch_num,
                  audio_info.sample_bits);
    }
#endif

    audio_initialized = true;
    PR_NOTICE("GB audio initialized");

    return OPRT_OK;
}

/**
 * @brief Deinitialize audio
 */
void gb_audio_deinit(void)
{
    if (!audio_initialized) {
        return;
    }

    pcm_close();

#if defined(AUDIO_CODEC_NAME)
    if (audio_handle) {
        tdl_audio_close(audio_handle);
        audio_handle = NULL;
    }
#endif

    audio_initialized = false;
    PR_NOTICE("GB audio deinitialized");
}

// GNUBoy sys.h interface implementations

void pcm_init(void)
{
    // PCM initialization
    // Audio system is already initialized by Tuya hardware registration
    pcm.hz     = AUDIO_SAMPLE_RATE;
    pcm.stereo = AUDIO_CHANNELS - 1; // 0 = mono, 1 = stereo
    pcm.len    = 1024;               // Buffer length in samples
    pcm.pos    = 0;
    // pcm.buf will be allocated by gnuboy's sound system
}

int pcm_submit(void)
{
    // Submit audio samples to Tuya audio system
    // This is called by gnuboy's sound system
    // The sound system fills pcm.buf with audio data

#if defined(AUDIO_CODEC_NAME)
    if (pcm.buf && pcm.pos > 0 && audio_handle) {
        // Calculate bytes to write
        int channels = pcm.stereo ? 2 : 1;
        int bytes    = pcm.pos * channels * sizeof(int16_t);

        // Write to Tuya audio using TDL audio API
        OPERATE_RET ret = tdl_audio_play(audio_handle, (uint8_t *)pcm.buf, bytes);
        if (ret != OPRT_OK) {
            PR_ERR("Audio play failed: %d", ret);
        }

        // Reset position
        int samples = pcm.pos;
        pcm.pos     = 0;
        return samples;
    }
    return 0;
#else
    return 0;
#endif
}

void pcm_close(void)
{
    // Close PCM (if needed)
}

void pcm_pause(void)
{
    // Pause audio (if supported)
}

void pcm_resume(void)
{
    // Resume audio (if supported)
}
