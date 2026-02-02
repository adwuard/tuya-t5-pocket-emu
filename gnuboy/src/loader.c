#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <string.h>

// Include Tuya memory compatibility layer BEFORE stdlib.h
// This redirects malloc/free/realloc to Tuya's TKL APIs
#include "tuya_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

// Include Tuya APIs for explicit system allocation and file system
#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_fs.h"
#include "tkl_memory.h"

#include "defs.h"
#include "regs.h"
#include "mem.h"
#include "hw.h"
#include "rtc.h"
#include "rc.h"
#include "lcd.h"
#include "inflate.h"
#include "lib/xz/xz.h"
#include "save.h"
#include "sound.h"
#include "sys.h"

static byte *decompress(byte *data, int *len);
static byte *loadfile(TUYA_FILE f, int *len);

static int mbc_table[256] = {
    0, 1, 1, 1, 0, 2, 2,          0,          0,          0, 0, 0, 0, 0, 0, 3, 3, 3, 3,        3,       0, 0,
    0, 0, 0, 5, 5, 5, MBC_RUMBLE, MBC_RUMBLE, MBC_RUMBLE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,       0, 0,
    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,

    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,       0, 0,
    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,       0, 0,
    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,

    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,       0, 0,
    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,       0, 0,
    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,

    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,       0, 0,
    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        0,       0, 0,
    0, 0, 0, 0, 0, 0, 0,          0,          0,          0, 0, 0, 0, 0, 0, 0, 0, 0, MBC_HUC3, MBC_HUC1};

static int rtc_table[256] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static int batt_table[256] = {0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1,
                              0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0};

static int romsize_table[256] = {
    2, 4, 8, 16, 32, 64, 128, 256, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,  0,
    0, 0, 0, 0,  0,  0,  0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,  0,
    0, 0, 0, 0,  0,  0,  0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 128, 128
    /* 0, 0, 72, 80, 96  -- actual values but bad to use these! */
};

static int ramsize_table[256] = {
    1, 1, 1, 4, 16, 4 /* FIXME - what value should this be?! */
};

static char *romfile;
static char *sramfile;
static char *rtcfile;
static char *saveprefix;
static char *bootroms[2];

static char *savename;
char        *savedir; // Made non-static to allow external access

static int saveslot;

static int forcebatt, nobatt;
static int forcedmg, gbamode;

static int memfill = -1, memrand = -1;

static TUYA_FILE rom_loadfile(char *fn, byte **data, int *len)
{
    TUYA_FILE f = NULL;

    if (strcmp(fn, "-")) {
        f = tkl_fopen(fn, "rb");
    } else {
        // stdin not supported on embedded, return error
        return NULL;
    }

    if (!f)
        return NULL;

    *data = loadfile(f, len);
    *data = decompress(*data, len);

    return f;
}

int bootrom_load()
{
    byte *data;
    int   len;
    FILE *f;

    REG(RI_BOOT) = 0xff;

    if (!bootroms[hw.cgb] || !bootroms[hw.cgb][0])
        return 0;

    f = rom_loadfile(bootroms[hw.cgb], &data, &len);

    if (!f)
        return 0;

    // Allocate bootrom bank (always 16KB) using PSRAM
    bootrom.bank = (byte(*)[16384])tkl_system_psram_malloc(16384);
    if (!bootrom.bank)
        die("out of memory for bootrom\n");
    // Copy loaded data
    if (data && len > 0) {
        memcpy(bootrom.bank[0], data, len < 16384 ? len : 16384);
        tkl_system_psram_free(data); // Free original data from PSRAM
    }
    // Fill remainder with 0xFF
    if (len < 16384) {
        memset(bootrom.bank[0] + len, 0xff, 16384 - len);
    }
    // Copy ROM header if available
    if (rom.bank) {
        memcpy(bootrom.bank[0] + 0x100, rom.bank[0] + 0x100, 0x100);
    }

    fclose(f);

    REG(RI_BOOT) = 0xfe;

    return 0;
}

static void initmem(void *mem, int size)
{
    char *p = mem;
    if (memrand >= 0) {
        srand(memrand ? memrand : time(0));
        while (size--)
            *(p++) = rand();
    } else if (memfill >= 0)
        memset(p, memfill, size);
}

static byte *loadfile(TUYA_FILE f, int *len)
{
    // Get file size first using Tuya TKL file system API
    tkl_fseek(f, 0, SEEK_END);
    long file_size = tkl_ftell(f);
    tkl_fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        *len = 0;
        return NULL;
    }

    // Allocate memory once for the entire file using PSRAM (larger than RAM)
    byte *d = (byte *)tkl_system_psram_malloc(file_size);
    if (!d) {
        *len = 0;
        return NULL;
    }

    // Read entire file using Tuya TKL file system API
    // tkl_fread(buf, bytes, file) - different signature from fread
    INT_T bytes_read = tkl_fread(d, file_size, f);
    if (bytes_read != file_size) {
        tkl_system_psram_free(d);
        *len = 0;
        return NULL;
    }

    *len = (int)file_size;
    return d;
}

static byte *inf_buf;
static int   inf_pos, inf_len;

static void inflate_callback(byte b)
{
    if (inf_pos >= inf_len) {
        // Grow buffer by larger chunks to reduce reallocations
        inf_len += 4096; // Grow by 4KB at a time instead of 512 bytes
        byte *new_buf = (byte *)tkl_system_psram_malloc(inf_len);
        if (!new_buf)
            die("out of memory inflating file @ %d bytes\n", inf_pos);
        // Copy old data if it exists
        if (inf_buf && inf_pos > 0) {
            memcpy(new_buf, inf_buf, inf_pos);
            tkl_system_psram_free(inf_buf);
            inf_buf = NULL; // Set to NULL after free (following example pattern)
        }
        inf_buf = new_buf;
    }
    inf_buf[inf_pos++] = b;
}

static byte *gunzip(byte *data, int *len)
{
    long pos = 0;
    inf_buf  = 0;
    inf_pos = inf_len = 0;
    if (unzip(data, &pos, inflate_callback) < 0)
        return data;
    *len = inf_pos;
    return inf_buf;
}

static void write_dec(byte *data, int len)
{
    int i;
    for (i = 0; i < len; i++)
        inflate_callback(data[i]);
}

static int unxz(byte *data, int len)
{
    struct xz_buf  b;
    struct xz_dec *s;
    enum xz_ret    ret;
    unsigned char  out[4096];

    /*
     * Support up to 64 MiB dictionary. The actually needed memory
     * is allocated once the headers have been parsed.
     */
    s = xz_dec_init(XZ_DYNALLOC, 1 << 26);
    if (!s)
        goto err;

    b.in       = data;
    b.in_pos   = 0;
    b.in_size  = len;
    b.out      = out;
    b.out_pos  = 0;
    b.out_size = sizeof(out);

    while (1) {
        ret = xz_dec_run(s, &b);
        if (b.out_pos == sizeof(out)) {
            write_dec(out, sizeof(out));
            b.out_pos = 0;
        }

        if (ret == XZ_OK)
            continue;

        write_dec(out, b.out_pos);

        if (ret == XZ_STREAM_END) {
            xz_dec_end(s);
            return 0;
        }
        goto err;
    }

err:
    xz_dec_end(s);
    return -1;
}

static byte *do_unxz(byte *data, int *len)
{
    xz_crc32_init();
    xz_crc64_init();
    inf_buf = 0;
    inf_pos = inf_len = 0;
    if (unxz(data, *len) < 0)
        return data;
    *len = inf_pos;
    return inf_buf;
}

static byte *decompress(byte *data, int *len)
{
    if (data[0] == 0x1f && data[1] == 0x8b)
        return gunzip(data, len);
    if (data[0] == 0xFD && !memcmp(data + 1, "7zXZ", 4))
        return do_unxz(data, len);
    return data;
}

int rom_load()
{
    TUYA_FILE f;
    byte      c, *data, *header;
    int       len = 0, rlen;
    f             = rom_loadfile(romfile, &data, &len);
    header        = data;

    if (!f)
        die("cannot open rom file: %s\n", romfile);

    // data and len are already set by rom_loadfile
    header = data = decompress(data, &len);

    memcpy(rom.name, header + 0x0134, 16);
    if (rom.name[14] & 0x80)
        rom.name[14] = 0;
    if (rom.name[15] & 0x80)
        rom.name[15] = 0;
    rom.name[16] = 0;

    c           = header[0x0147];
    mbc.type    = mbc_table[c];
    mbc.batt    = (batt_table[c] && !nobatt) || forcebatt;
    rtc.batt    = rtc_table[c];
    mbc.romsize = romsize_table[header[0x0148]];
    mbc.ramsize = ramsize_table[header[0x0149]];

    if (!mbc.romsize)
        die("unknown ROM size %02X\n", header[0x0148]);
    if (!mbc.ramsize)
        die("unknown SRAM size %02X\n", header[0x0149]);

    rlen = 16384 * mbc.romsize;
    // Allocate ROM bank at final size using PSRAM (ROMs can be large)
    rom.bank = (byte(*)[16384])tkl_system_psram_malloc(rlen);
    if (!rom.bank)
        die("out of memory for ROM bank\n");
    // Copy loaded data
    if (data && len > 0) {
        memcpy(rom.bank[0], data, len < rlen ? len : rlen);
        tkl_system_psram_free(data); // Free original data from PSRAM
    }
    // Fill remainder with 0xFF if needed
    if (rlen > len) {
        memset(rom.bank[0] + len, 0xff, rlen - len);
    }

    ram.sbank = tkl_system_psram_malloc(8192 * mbc.ramsize);

    initmem(ram.sbank, 8192 * mbc.ramsize);
    initmem(ram.ibank, 4096 * 8);

    mbc.rombank = 1;
    mbc.rambank = 0;

    c      = header[0x0143];
    hw.cgb = ((c == 0x80) || (c == 0xc0)) && !forcedmg;
    hw.gba = (hw.cgb && gbamode);

    if (strcmp(romfile, "-"))
        tkl_fclose(f);

    return 0;
}

int sram_load()
{
    FILE *f;

    if (!mbc.batt || !sramfile || !*sramfile)
        return -1;

    /* Consider sram loaded at this point, even if file doesn't exist */
    ram.loaded = 1;

    f = fopen(sramfile, "rb");
    if (!f)
        return -1;
    fread(ram.sbank, 8192, mbc.ramsize, f);
    fclose(f);

    return 0;
}

int sram_save()
{
    TUYA_FILE f;

    /* If we crash before we ever loaded sram, DO NOT SAVE! */
    if (!mbc.batt || !sramfile || !ram.loaded || !mbc.ramsize)
        return -1;

    f = tkl_fopen(sramfile, "wb");
    if (!f)
        return -1;
    tkl_fwrite(ram.sbank, 8192 * mbc.ramsize, f);
    tkl_fclose(f);

    return 0;
}

void state_save(int n)
{
    FILE *f;
    char *name;

    if (n < 0)
        n = saveslot;
    if (n < 0)
        n = 0;
    name = tal_malloc(strlen(saveprefix) + 5);
    sprintf(name, "%s.%03d", saveprefix, n);

    if ((f = fopen(name, "wb"))) {
        savestate(f);
        fclose(f);
    }
    tal_free(name);
}

void state_load(int n)
{
    FILE *f;
    char *name;

    if (n < 0)
        n = saveslot;
    if (n < 0)
        n = 0;
    name = tal_malloc(strlen(saveprefix) + 5);
    sprintf(name, "%s.%03d", saveprefix, n);

    if ((f = fopen(name, "rb"))) {
        loadstate(f);
        fclose(f);
        vram_dirty();
        pal_dirty();
        sound_dirty();
        mem_updatemap();
    }
    tal_free(name);
}

void rtc_save()
{
    FILE *f;
    if (!rtc.batt)
        return;
    if (!(f = fopen(rtcfile, "wb")))
        return;
    rtc_save_internal(f);
    fclose(f);
}

void rtc_load()
{
    FILE *f;
    if (!rtc.batt)
        return;
    if (!(f = fopen(rtcfile, "r")))
        return;
    rtc_load_internal(f);
    fclose(f);
}

void loader_unload()
{
    sram_save();
    if (romfile)
        tal_free(romfile);
    if (sramfile)
        tal_free(sramfile);
    if (saveprefix)
        tal_free(saveprefix);
    if (rom.bank)
        tkl_system_psram_free(rom.bank);
    if (ram.sbank)
        tkl_system_psram_free(ram.sbank);
    romfile = sramfile = saveprefix = 0;
    rom.bank                        = 0;
    ram.sbank                       = 0;
    mbc.type = mbc.romsize = mbc.ramsize = mbc.batt = 0;
}

static char *base(char *s)
{
    char *p;
    p = strrchr(s, '/');
    if (p)
        return p + 1;
    return s;
}

static char *ldup(char *s)
{
    int   i;
    char *n, *p;
    p = n = tal_malloc(strlen(s));
    for (i = 0; s[i]; i++)
        if (isalnum((unsigned char)s[i]))
            *(p++) = tolower((unsigned char)s[i]);
    *p = 0;
    return n;
}

static void cleanup()
{
    sram_save();
    rtc_save();
    /* IDEA - if error, write emergency savestate..? */
}

void loader_init(char *s)
{
    char *name, *p;

    sys_checkdir(savedir, 1); /* needs to be writable */

    romfile = s;
    rom_load();
    bootrom_load();
    vid_settitle(rom.name);
    if (savename && *savename) {
        if (savename[0] == '-' && savename[1] == 0)
            name = ldup(rom.name);
        else
            name = strdup(savename);
    } else if (romfile && *base(romfile) && strcmp(romfile, "-")) {
        name = strdup(base(romfile));
        p    = strchr(name, '.');
        if (p)
            *p = 0;
    } else
        name = ldup(rom.name);

    saveprefix = tal_malloc(strlen(savedir) + strlen(name) + 2);
    sprintf(saveprefix, "%s/%s", savedir, name);

    sramfile = tal_malloc(strlen(saveprefix) + 5);
    strcpy(sramfile, saveprefix);
    strcat(sramfile, ".sav");

    rtcfile = tal_malloc(strlen(saveprefix) + 5);
    strcpy(rtcfile, saveprefix);
    strcat(rtcfile, ".rtc");

    sram_load();
    rtc_load();

    atexit(cleanup);
}

rcvar_t loader_exports[] = {RCV_STRING("savedir", &savedir),
                            RCV_STRING("bootrom_dmg", &bootroms[0]),
                            RCV_STRING("bootrom_cgb", &bootroms[1]),
                            RCV_STRING("savename", &savename),
                            RCV_INT("saveslot", &saveslot),
                            RCV_BOOL("forcebatt", &forcebatt),
                            RCV_BOOL("nobatt", &nobatt),
                            RCV_BOOL("forcedmg", &forcedmg),
                            RCV_BOOL("gbamode", &gbamode),
                            RCV_INT("memfill", &memfill),
                            RCV_INT("memrand", &memrand),
                            RCV_END};
