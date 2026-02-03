/**
 * @file gb_storage.c
 * @brief Game Boy Storage Adapter
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "gb_storage.h"

#include "sys.h"
#include "path.h"

/**
 * @brief Initialize storage
 */
OPERATE_RET gb_storage_init(void)
{
    // Initialize system paths
    sys_initpath();
    return OPRT_OK;
}

/**
 * @brief Deinitialize storage
 */
void gb_storage_deinit(void)
{
}

// GNUBoy sys.h interface implementations

void sys_checkdir(char *path, int wr)
{
    // Check/create directory
    // Path is already handled by Tuya file system
    (void)path;
    (void)wr;
}

void sys_sleep(int us)
{
    // Sleep for microseconds
    tal_system_sleep(us / 1000); // Convert to milliseconds
}

void sys_sanitize(char *s)
{
    // Sanitize path string
    // Remove invalid characters
    if (s == NULL) {
        return;
    }

    for (char *p = s; *p; p++) {
        if (*p == '\\' || *p == ':' || *p == '*' || *p == '?' || 
            *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }
}

void sys_initpath(void)
{
    // Initialize system paths
    // Paths are configured via Kconfig
}

int sys_elapsed(int *prev)
{
    // Get elapsed time in microseconds (gnuboy expects microseconds)
    static int first = 1;
    uint32_t now_ms = tal_system_get_millisecond();
    int now = (int)now_ms;

    if (first) {
        *prev = now;
        first = 0;
        return 0;
    }

    int elapsed_ms = now - *prev;
    *prev = now;
    return elapsed_ms * 1000; // Convert to microseconds
}
