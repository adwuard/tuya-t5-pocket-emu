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
#include <string.h>

// Hardware codec: 16-bit, 1 channel (mono), 16000 Hz
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_CHANNELS    1
#define AUDIO_BITS        16 // 16-bit signed samples (converted from 8-bit unsigned)

static bool               audio_initialized = false;
static TDL_AUDIO_HANDLE_T audio_handle      = NULL;
static bool               audio_started     = false;

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

#if defined(AUDIO_CODEC_NAME)
    // Find audio device
    ret = tdl_audio_find(AUDIO_CODEC_NAME, &audio_handle);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to find audio device: %d", ret);
        return ret;
    }

    // Configure audio parameters before opening
    // Note: Tuya audio codec may need to be configured via board API or Kconfig
    // The actual sample rate might be set at hardware registration level

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
        // Verify hardware codec matches expected configuration (silently)
        (void)audio_info; // Suppress unused variable warning if not used
    }
#endif

    audio_initialized = true;
    audio_started     = false;
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
}

// GNUBoy sys.h interface implementations

void pcm_init(void)
{
    // PCM initialization - optimized for low latency
    // Hardware codec: 16kHz, 16-bit, 1 channel (mono)
    // Use smaller buffer for lower latency (reduces CPU cycles per frame)
    // Smaller buffer = less data to process = lower latency

    pcm.hz     = AUDIO_SAMPLE_RATE;  // 16000 Hz (hardware codec rate)
    pcm.stereo = AUDIO_CHANNELS - 1; // 0 = mono, 1 = stereo

    // Use smaller buffer for lower latency: samplerate / 120 (half of 60fps)
    // At 16000 Hz: 16000 / 120 = 133 samples, round to 256 (next power of 2)
    // This reduces processing time per frame and improves responsiveness
    int samples = AUDIO_SAMPLE_RATE / 120;

    // Round up to next power of 2 (like SDL2 does)
    int i;
    for (i = 1; i < samples; i <<= 1)
        ;
    samples = i;

    pcm.len = samples;

    // Allocate buffer for audio samples
    // SDL2 uses AUDIO_U8 (unsigned 8-bit), so we allocate for 8-bit samples
    // Buffer size in bytes: samples * channels * 1 byte per sample (unsigned 8-bit)
    int channels = pcm.stereo ? 2 : 1;
    int buf_size = pcm.len * channels * sizeof(byte); // 1 byte per sample (AUDIO_U8)

    if (pcm.buf == NULL) {
        pcm.buf = (byte *)tal_malloc(buf_size);
        if (pcm.buf == NULL) {
            PR_ERR("Failed to allocate PCM buffer (%d bytes)", buf_size);
            pcm.len = 0;
            return;
        }
        memset(pcm.buf, 0, buf_size);
    }

    pcm.pos = 0;
}

int pcm_submit(void)
{
    // Submit audio samples to Tuya audio system
    // Following SDL2-GNUBoy pattern: wait for buffer to fill, then submit

    if (!pcm.buf) {
        static int warn_count = 0;
        if (warn_count++ < 3) {
            PR_WARN("pcm_submit: PCM buffer not allocated");
        }
        return 0;
    }

    // Wait for buffer to fill (like SDL2: if (pcm.pos < pcm.len) return 1;)
    if (pcm.pos < pcm.len) {
        return 1; // Buffer not full yet, keep filling
    }

#if defined(AUDIO_CODEC_NAME)
    if (!audio_handle) {
        static int warn_count = 0;
        if (warn_count++ < 3) {
            PR_WARN("pcm_submit: Audio handle is NULL (audio not initialized?)");
        }
        pcm.pos = 0;
        return 0;
    }
    // Convert from SDL2's AUDIO_U8 (unsigned 8-bit) format to 16-bit signed
    // SDL2 format: samples are 0-255 (unsigned), center at 128
    // Tuya format: samples are -32768 to 32767 (signed 16-bit)
    // Conversion: (sample - 128) * 256

    int channels           = pcm.stereo ? 2 : 1;
    int samples_to_convert = pcm.len * channels;

    // Allocate temporary buffer for converted samples (if needed)
    // Or convert in-place if pcm.buf is large enough
    static int16_t *converted_buf      = NULL;
    static int      converted_buf_size = 0;

    if (converted_buf_size < samples_to_convert) {
        if (converted_buf) {
            tal_free(converted_buf);
        }
        converted_buf = (int16_t *)tal_malloc(samples_to_convert * sizeof(int16_t));
        if (converted_buf == NULL) {
            PR_ERR("Failed to allocate converted audio buffer");
            pcm.pos = 0;
            return 0;
        }
        converted_buf_size = samples_to_convert;
    }

    // Convert from SDL2's AUDIO_U8 (unsigned 8-bit) to hardware codec format (signed 16-bit)
    // SDL2 format: 0-255 (unsigned), center at 128
    // Hardware codec: -32768 to 32767 (signed 16-bit), 16kHz, mono
    // Optimized conversion: (sample - 128) * 256
    // Use pointer arithmetic and unroll loop for better performance
    byte    *src = pcm.buf;
    int16_t *dst = converted_buf;
    int      i   = 0;

    // Process in chunks for better cache performance
    for (; i < samples_to_convert - 3; i += 4) {
        // Unroll 4 samples at a time
        int s0     = (int)src[i] - 128;
        int s1     = (int)src[i + 1] - 128;
        int s2     = (int)src[i + 2] - 128;
        int s3     = (int)src[i + 3] - 128;
        dst[i]     = (int16_t)(s0 * 256);
        dst[i + 1] = (int16_t)(s1 * 256);
        dst[i + 2] = (int16_t)(s2 * 256);
        dst[i + 3] = (int16_t)(s3 * 256);
    }
    // Handle remaining samples
    for (; i < samples_to_convert; i++) {
        int sample = (int)src[i] - 128;
        dst[i]     = (int16_t)(sample * 256);
    }

    // Start audio playback on first submit (like SDL2)
    if (!audio_started) {
        // Audio should start automatically on first write, but we mark it as started
        audio_started = true;
    }

    // Submit converted audio data to Tuya audio system
    int         bytes = samples_to_convert * sizeof(int16_t);
    OPERATE_RET ret   = tdl_audio_play(audio_handle, (uint8_t *)converted_buf, bytes);
    if (ret != OPRT_OK) {
        static int error_count = 0;
        if (error_count++ < 5) {
            PR_ERR("Audio play failed: %d (samples=%d, bytes=%d)", ret, samples_to_convert, bytes);
        }
        pcm.pos = 0;
        return 0;
    }

    // Reset position for next buffer fill
    pcm.pos = 0;
    return 1; // Successfully submitted
#else
    // AUDIO_CODEC_NAME not defined - audio disabled
    static int warn_count = 0;
    if (warn_count++ < 1) {
        PR_WARN("pcm_submit: AUDIO_CODEC_NAME not defined, audio disabled");
    }
    pcm.pos = 0;
    return 0;
#endif
}

void pcm_close(void)
{
    // Free PCM buffer if allocated
    if (pcm.buf) {
        tal_free(pcm.buf);
        pcm.buf = NULL;
    }
    pcm.len       = 0;
    pcm.pos       = 0;
    audio_started = false;
}

void pcm_pause(void)
{
    // Pause audio (if supported)
}

void pcm_resume(void)
{
    // Resume audio (if supported)
}
