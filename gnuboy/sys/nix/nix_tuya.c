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
#include "input.h"
#include "hw.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// Forward declarations
extern void ev_poll(void);
extern int  ev_getevent(event_t *ev);
extern void io_recv(void);

// Minimal rc_dokey implementation - directly maps key codes to pad_set()
// This bypasses the RC system which is not needed for embedded builds
void rc_dokey(int key, int st)
{
    byte pad_button = 0;

    // Map key codes to PAD_* constants
    switch (key) {
    case K_JOYUP:
        pad_button = PAD_UP;
        break;
    case K_JOYDOWN:
        pad_button = PAD_DOWN;
        break;
    case K_JOYLEFT:
        pad_button = PAD_LEFT;
        break;
    case K_JOYRIGHT:
        pad_button = PAD_RIGHT;
        break;
    case K_JOY0:
        pad_button = PAD_SELECT; // Physical SELECT → Game Boy SELECT
        break;
    case K_JOY1:
        pad_button = PAD_B;
        break;
    case K_JOY2:
        pad_button = PAD_A; // Physical A → Game Boy A
        break;
    case K_JOY3:
        pad_button = PAD_START;
        break;
    default:
        // Unknown key, ignore
        return;
    }

    // Update Game Boy pad state
    pad_set(pad_button, st);
}

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

// Event handling - matches SDL2 behavior
// Note: ev_poll() is called in main loop, so we only process events here
void doevents(void)
{
    event_t ev;
    int     st;

    // Process all events from the queue (events were already posted by ev_poll() in main loop)
    while (ev_getevent(&ev)) {
        if (ev.type != EV_PRESS && ev.type != EV_RELEASE)
            continue;
        st = (ev.type != EV_RELEASE);
        rc_dokey(ev.code, st);
    }

    // Handle I/O (like SDL2 does)
    io_recv();
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
