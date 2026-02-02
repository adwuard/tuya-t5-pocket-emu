/**
 * @file nix_tuya.c
 * @brief Tuya-specific system implementations for gnuboy
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "defs.h"
#include "sys.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// Timer implementation using Tuya's time API
void *sys_timer(void)
{
    int *tv = (int *)tal_malloc(sizeof(int));
    if (tv) {
        *tv = (int)tal_system_get_millisecond();
    }
    return tv;
}

// Note: sys_elapsed, sys_sleep, sys_checkdir, sys_initpath, sys_sanitize
// are already implemented in gb_storage.c

// Event handling - stub implementation
void doevents(void)
{
    // Events are handled via gb_input_poll() in our adapter
    // This is called from emu_run() but we handle events separately
}

// Error handling - stub implementation
void die(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    PR_ERR("GB Emulator fatal error: ");
    // Note: tal_log doesn't support va_list directly, so we use a buffer
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    PR_ERR("%s", buf);

    va_end(ap);

    // For embedded, we might want to reset instead of exit
    // For now, just log the error
}

// Debug functions - stub implementations (debug.c is disabled)
int debug_trace = 0;

void debug_disassemble(addr a, int c)
{
    // Debug disassembly - disabled for embedded
    (void)a;
    (void)c;
}
