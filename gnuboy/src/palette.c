#include <stdlib.h>

#include "defs.h"
#include "fb.h"
#include "sys.h"

// Color palette arrays - not needed for DMG (monochrome) emulation
// These are only used for indexed color mode (CGB), which we're not supporting
// Removed to save memory: palmap[32768] and crsmap[4][32768] = 160 KB saved
static byte pallock[256];
static int  palrev[256];

enum plstatus { pl_unused = 0, pl_linger, pl_active, pl_locked };

static byte __attribute__((unused)) bestmatch(int c)
{
    byte n, best;
    int  r, g, b;
    int  r2, g2, b2, c2;
    int  err, besterr;

    r = (c & 0x001F) << 3;
    g = (c & 0x03E0) >> 2;
    b = (c & 0x7C00) >> 7;

    best    = 0;
    besterr = 1024;
    for (n = 1; n; n++) {
        c2  = palrev[n];
        r2  = (c2 & 0x001F) << 3;
        g2  = (c2 & 0x03E0) >> 2;
        b2  = (c2 & 0x7C00) >> 7;
        err = abs(r - r2) + abs(b - b2) + abs(g - g2);
        if (err < besterr) {
            besterr = err;
            best    = n;
        }
    }
    return best;
}

// makecourse and findcourse removed - not needed for DMG emulation

void pal_lock(byte n)
{
    if (!n)
        return;
    if (pallock[n] >= pl_locked)
        pallock[n]++;
    else
        pallock[n] = pl_locked;
}

byte pal_getcolor(int c, int r, int g, int b)
{
    // Simplified for DMG (monochrome) - no indexed color mode
    // Just return a simple mapping
    static byte n = 0;
    (void)c;
    (void)r;
    (void)g;
    (void)b;    // Unused for DMG
    return n++; // Simple round-robin (not actually used in DMG mode)
}

void pal_release(byte n)
{
    if (pallock[n] >= pl_locked)
        pallock[n]--;
}

void pal_expire()
{
    int i;
    for (i = 0; i < 256; i++)
        if (pallock[i] && pallock[i] < pl_locked)
            pallock[i]--;
}

void pal_set332()
{
    int i, r, g, b;

    fb.indexed = 0;
    fb.cc[0].r = 5;
    fb.cc[1].r = 5;
    fb.cc[2].r = 6;
    fb.cc[0].l = 0;
    fb.cc[1].l = 3;
    fb.cc[2].l = 6;

    i = 0;
    for (b = 0; b < 4; b++)
        for (g = 0; g < 8; g++)
            for (r = 0; r < 8; r++)
                vid_setpal(i++, (r << 5) | (r << 2) | (r >> 1), (g << 5) | (g << 2) | (g >> 1),
                           (b << 6) | (b << 4) | (b << 2) | b);
}
