/*
 * Settings persistence in the CH555 Data-Flash (EEPROM).
 *
 * The chip has 1 KiB of Data-Flash at 0xF000, in the code address space:
 * reading is a plain MOVC, writing needs the safe-mode unlock plus bDATA_WE and
 * a ROM_CTRL command (0xA6 erase a 64-byte block, 0x9A program one byte).
 *
 * Four things are stored, each in its own 64-byte block range:
 *
 *   0xF000  lighting + input settings   (1 block)
 *   0xF040  per-key remap, 67 bytes     (2 blocks; it used to spill into the
 *                                        next section's magic)
 *   0xF0C0  custom-effect picture       (3 blocks, 63 x RGB)
 *   0xF180  macro slots                 (4 blocks)
 *
 * Each section carries its own magic, version and checksum, so a section that
 * does not verify is simply not loaded: it falls back to compiled defaults and
 * is rewritten once.  That way a power cut during a save cannot leave the
 * keyboard with nonsense settings, and it also means one section can be
 * re-flashed without touching the others.
 *
 * Two details make the difference between a block that survives and one that
 * comes back as garbage:
 *
 *   - Safe mode only lasts 13..23 system clocks.  An interrupt taken between
 *     SAFE_MOD = 0x55 and SAFE_MOD = 0xAA eats that window, so the following
 *     GLOBAL_CFG write is ignored and the flash stays write protected.  The
 *     unlock therefore runs with interrupts disabled; nothing else needs to.
 *   - GLOBAL_CFG can only be written in safe mode, so clearing bDATA_WE again
 *     needs its own unlock.  The old code wrote it directly, which silently
 *     left the data flash write enabled for ever.
 *
 * Writes are debounced by SAVE_DELAY_MS and read back, so holding a Fn combo
 * does not wear the flash out.
 */
#include "ch555.h"
#include "settings.h"
#include "led.h"
#include "keymap.h"
#include "keyboard.h"
#include "macro.h"

#define EE_BASE         DATA_FLASH_ADDR
#define EE_BLOCK        64

#define EE_MAGIC0       0x56        /* 'V' */
#define EE_MAGIC1       0x4D        /* 'M' */
#define EE_VER          5

/* Layout, one 64-byte block each unless noted:
 *
 *   0xF000  settings              (1 block)
 *   0xF040  remap, 4 + 63 = 67 B  (2 blocks - it used to spill into the
 *                                  picture's magic and break its checksum)
 *   0xF0C0  custom picture, 192 B (3 blocks)
 *   0xF180  macros, 204 B         (4 blocks)
 *
 * The data flash ends at 0xF3FF (the bootloader starts at 0xF400), so this
 * leaves room for two more blocks.
 */
#define MAP_BASE        (EE_BASE + 1 * EE_BLOCK)
#define MAP_BLOCKS      2
#define MAP_MAGIC0      0x52        /* 'R' */
#define MAP_MAGIC1      0x4D
#define MAP_VER         1

#define CUS_BASE        (EE_BASE + 3 * EE_BLOCK)
#define CUS_BYTES       (63 * 3)    /* 189, padded to three blocks */
#define CUS_VER         1

/* Macros: 3 header bytes + 4 slots x (key, len, 24 x 2) = 203 bytes,
 * rounded up to four blocks. */
#define MAC_BASE        (EE_BASE + 6 * EE_BLOCK)
#define MAC_BLOCKS      4
#define MAC_MAGIC       0x4B        /* 'K' */
#define MAC_VER         1
#define MAC_PAYLOAD     (MACRO_SLOTS * (2 + MACRO_STEPS * 2))

#define SAVE_DELAY_MS   1500
#define SAVE_TRIES      2

/* Field offsets inside the settings block. */
#define EE_OFF_MAGIC0   0
#define EE_OFF_MAGIC1   1
#define EE_OFF_VER      2
#define EE_OFF_EFFECT   3
#define EE_OFF_BRIGHT   4
#define EE_OFF_SPEED    5
#define EE_OFF_BACK     6
#define EE_OFF_CRC      7
#define EE_OFF_GAIN     8           /* 8 per-channel trims, 8..15            */
#define EE_OFF_CAL      16          /* 3 white-balance trims, 16..18         */
#define EE_OFF_BACKLVL  19
#define EE_OFF_BACKRGB  20          /* 3 bytes, 20..22                       */
#define EE_OFF_DEBOUNCE 23
#define EE_OFF_GAMING   24
#define EE_OFF_BOOTMODE 25
#define EE_OFF_BOOTPACE 26
#define EE_OFF_BACKMODE 27

/* Shadow of every stored value, compared each poll to notice a change made by
 * the Fn combos.  Exact values, not hashes, so two different settings can
 * never cancel each other out. */
#define SHADOW_LEN      24
static __xdata UINT8 shadow[SHADOW_LEN];

static UINT16 save_timer;
static UINT16 save_hold;            /* ticks a save has been held off by keys */
static __bit dirty;                 /* settings block      */
static __bit dirty_map;             /* remap block         */
static __bit dirty_custom;          /* custom-effect block */
static __bit dirty_macro;           /* macro table         */
static __bit legacy_block;          /* block was written by v4 firmware */

/* Diagnostic: completed save rounds since boot, saturating.  A value that
 * climbs on its own means something is marking a section dirty every tick. */
UINT8 settings_writes = 0;

/* Last result the ROM controller reported for a data-flash operation:
 * 0 accepted, 1 address rejected, 2 command error or timeout.  0xFF means no
 * operation has run since reset. */
UINT8 settings_rom_status = 0xFF;

/* Set when the settings block verified during settings_load().  A zero here
 * with a steady settings_rom_status means the bytes never reached the flash. */
UINT8 settings_block_valid = 0;

/* How often the save path was entered and how often the change detector
 * re-armed the debounce timer.  A steady settings_rounds with a block that
 * still will not verify points at the flash; a shadow counter that climbs
 * without rounds points at the change detector.  Kept in xdata because
 * internal RAM has no room left. */
__xdata UINT8 settings_rounds = 0;
__xdata UINT8 settings_shadow_hits = 0;

/* Which write shape last produced a block that verified: 0xFF unknown (the
 * one-window shape is tried first), 0 one window per page, 1 a fresh window
 * per byte.  Learned once, then reused by every section. */
__xdata UINT8 settings_shape = 0xFF;

/* ---- EEPROM primitives ------------------------------------------------ */

static UINT8 ee_read(UINT16 addr)
{
    __code const UINT8 *p = (__code const UINT8 *)addr;
    return *p;
}

/* Bits that must never be carried into a GLOBAL_CFG write: bBOOT_LOAD is
 * read-only, bSW_RESET resets the CPU (which is what silently aborted every
 * save before this mask existed), and bCODE_WE would let the erase reach the
 * code area.  The register is re-read on every call instead of cached, so a
 * bit that is only transiently set can never be latched in. */
#define CFG_KEEP_MASK  (UINT8)~(bBOOT_LOAD | bSW_RESET | bCODE_WE | bDATA_WE)

/* Open or close the data-flash write window.  The caller must hold EA low:
 * the safe-mode window is a few clocks and must not be stretched by an
 * interrupt. */
static void ee_write_enable(UINT8 on)
{
    UINT8 cfg;

    /* Build the exact byte first: the safe-mode window closes a few clocks
     * after the 0xAA, so nothing but the store may sit between them. */
    cfg = (UINT8)(GLOBAL_CFG & CFG_KEEP_MASK);
    cfg = (UINT8)(on ? (cfg | bDATA_WE) : cfg);
    SAFE_MOD = 0x55;
    SAFE_MOD = 0xAA;
    GLOBAL_CFG = cfg;
}

/* One page of data flash, written in a single go.
 *
 * The staging buffer holds the page's bytes.  A page can only be programmed
 * after it has been erased, so the caller fills this buffer first and then
 * asks for the page to be written.
 *
 * `each` selects how long the write window stays open: 0 keeps it open for
 * the whole page, 1 opens a fresh one for every byte.  Both are tried by the
 * settings block, which is where the answer is learned.
 *
 * The register sequence below is written out inline on purpose.  On this part
 * a data-flash operation only takes effect when the address, the buffer mode,
 * the data byte and the command follow each other with nothing in between.
 * Every arrangement that put a helper call, a status read or a conditional
 * between them left the block unchanged while still reporting success - the
 * self-test measured exactly that.  This is the shape it proved, applied to a
 * whole block instead of a single byte.
 */
static __xdata UINT8 ee_page[EE_BLOCK];

static void ee_write_page(UINT16 page, UINT8 len, __bit each)
{
    UINT8 i, st, status = 0;

    /* A page that already holds exactly these bytes needs neither the erase
     * nor the program: both hold the CPU (and stop the LED scan) for about
     * 160 ms per page, and "Save to flash" writes four sections whether they
     * changed or not.  Comparing first turns a lighting-only save from twelve
     * stopped pages into one, and stops wearing blocks that did not change.
     * The read is a plain MOVC, so it runs with interrupts on. */
    for (i = 0; i < len; i++) {
        if (ee_read((UINT16)(page + i)) != ee_page[i]) {
            break;
        }
    }
    if (i == len) {
        settings_rom_status = 0;
        return;
    }

    EA = 0;
    ee_write_enable(1);

    ROM_ADDR_L = (UINT8)(page & 0xFF);
    ROM_ADDR_H = (UINT8)(page >> 8);
    ROM_BUF_MOD = bROM_BUF_BYTE;
    ROM_DAT_BUF = 0x00;             /* erase: the data byte must be 0 */
    ROM_CTRL = ROM_CMD_ERASE;       /* the CPU stalls until it has finished */
    st = (UINT8)ROM_STATUS;
    if (!(st & bROM_ADDR_OK)) {
        status = 1;
    } else if (st & bROM_CMD_ERR) {
        status = 2;
    }

    for (i = 0; i < len; i++) {
        UINT8 v = ee_page[i];       /* read the byte before touching the SFRs */
        UINT16 a = (UINT16)(page + i);

        if (each) {
            /* Second shape: a fresh write window for every single byte, the
             * way WCH's own driver does it.  Kept as a fallback because this
             * controller has been observed to accept a whole page in one
             * window and write nothing. */
            ee_write_enable(1);
        }
        ROM_ADDR_L = (UINT8)(a & 0xFF);
        ROM_ADDR_H = (UINT8)(a >> 8);
        ROM_BUF_MOD = bROM_BUF_BYTE;
        ROM_DAT_BUF = v;
        ROM_CTRL = ROM_CMD_PROG;
        st = (UINT8)ROM_STATUS;
        if (!(st & bROM_ADDR_OK)) {
            status = 1;
        } else if ((st & bROM_CMD_ERR) && status == 0) {
            status = 2;
        }
    }

    ee_write_enable(0);
    EA = 1;
    settings_rom_status = status;
}

/* Put one byte into the page being staged.  Bytes that belong to another page
 * are dropped, so a section can be described once and written page by page. */
static void stage(UINT8 page, UINT16 off, UINT8 val)
{
    UINT16 base = (UINT16)((UINT16)page * EE_BLOCK);

    if (off >= base && off < (UINT16)(base + EE_BLOCK)) {
        ee_page[off - base] = val;
    }
}

/* XOR checksum over a run of stored bytes.  Used for the blocks that have no
 * live copy in RAM to compare against. */
static UINT8 ee_sum(UINT16 addr, UINT8 len)
{
    UINT8 s = 0;

    while (len--) {
        s = (UINT8)(s ^ ee_read(addr++));
    }
    return s;
}

/* ---- settings block --------------------------------------------------- */

static UINT8 checksum(void)
{
    UINT8 i, s;

    s = (UINT8)(EE_MAGIC0 ^ EE_MAGIC1 ^ EE_VER ^
                led_effect ^ led_brightness ^ led_speed ^ led_back ^
                led_back_level ^ kbd_debounce ^ kbd_gaming ^
                led_boot_mode ^ led_boot_pace ^ led_back_mode);
    for (i = 0; i < 8; i++) {
        s = (UINT8)(s ^ led_gain[i]);
    }
    for (i = 0; i < 3; i++) {
        s = (UINT8)(s ^ led_cal[i]);
    }
    for (i = 0; i < 3; i++) {
        s = (UINT8)(s ^ led_back_rgb[i]);
    }
    return s;
}

/* Version-aware so a block written by firmware 4 still verifies: the three
 * new fields only take part from version 5 on.  That lets load() keep the
 * user's lighting after an upgrade instead of resetting it. */
static UINT8 checksum_block_v(UINT8 ver)
{
    UINT8 i, s;

    s = (UINT8)(EE_MAGIC0 ^ EE_MAGIC1 ^ ver ^
                ee_read(EE_BASE + EE_OFF_EFFECT) ^
                ee_read(EE_BASE + EE_OFF_BRIGHT) ^
                ee_read(EE_BASE + EE_OFF_SPEED) ^
                ee_read(EE_BASE + EE_OFF_BACK) ^
                ee_read(EE_BASE + EE_OFF_BACKLVL) ^
                ee_read(EE_BASE + EE_OFF_DEBOUNCE) ^
                ee_read(EE_BASE + EE_OFF_GAMING));
    for (i = 0; i < 8; i++) {
        s = (UINT8)(s ^ ee_read(EE_BASE + EE_OFF_GAIN + i));
    }
    for (i = 0; i < 3; i++) {
        s = (UINT8)(s ^ ee_read(EE_BASE + EE_OFF_CAL + i));
    }
    for (i = 0; i < 3; i++) {
        s = (UINT8)(s ^ ee_read(EE_BASE + EE_OFF_BACKRGB + i));
    }
    if (ver >= 5) {
        s = (UINT8)(s ^ ee_read(EE_BASE + EE_OFF_BOOTMODE) ^
                    ee_read(EE_BASE + EE_OFF_BOOTPACE) ^
                    ee_read(EE_BASE + EE_OFF_BACKMODE));
    }
    return s;
}

static __bit block_ok(void)
{
    UINT8 ver;

    legacy_block = 0;
    if (ee_read(EE_BASE + EE_OFF_MAGIC0) != EE_MAGIC0 ||
        ee_read(EE_BASE + EE_OFF_MAGIC1) != EE_MAGIC1) {
        return 0;
    }
    ver = ee_read(EE_BASE + EE_OFF_VER);
    if (ver != EE_VER && ver != 4) {
        return 0;
    }
    if (ee_read(EE_BASE + EE_OFF_CRC) != checksum_block_v(ver)) {
        return 0;
    }
    legacy_block = (ver != EE_VER) ? 1 : 0;
    return 1;
}

static void write_block(void)
{
    UINT8 i;

    for (i = 0; i < EE_BLOCK; i++) {
        ee_page[i] = 0;
    }
    ee_page[EE_OFF_MAGIC0] = EE_MAGIC0;
    ee_page[EE_OFF_MAGIC1] = EE_MAGIC1;
    ee_page[EE_OFF_VER] = EE_VER;
    ee_page[EE_OFF_EFFECT] = led_effect;
    ee_page[EE_OFF_BRIGHT] = led_brightness;
    ee_page[EE_OFF_SPEED] = led_speed;
    ee_page[EE_OFF_BACK] = led_back;
    for (i = 0; i < 8; i++) {
        ee_page[EE_OFF_GAIN + i] = led_gain[i];
    }
    for (i = 0; i < 3; i++) {
        ee_page[EE_OFF_CAL + i] = led_cal[i];
        ee_page[EE_OFF_BACKRGB + i] = led_back_rgb[i];
    }
    ee_page[EE_OFF_BACKLVL] = led_back_level;
    ee_page[EE_OFF_DEBOUNCE] = kbd_debounce;
    ee_page[EE_OFF_GAMING] = kbd_gaming;
    ee_page[EE_OFF_BOOTMODE] = led_boot_mode;
    ee_page[EE_OFF_BOOTPACE] = led_boot_pace;
    ee_page[EE_OFF_BACKMODE] = led_back_mode;
    ee_page[EE_OFF_CRC] = checksum();

    /* One window for the whole page, then - only if that did not verify - a
     * fresh window per byte.  Whichever works is remembered for the other
     * sections.  The retry result itself is not re-read: ee_write_page()
     * verifies every byte it programs, so a failed retry leaves the section
     * invalid at the next boot instead of loading a torn block. */
    ee_write_page(EE_BASE, (UINT8)(EE_OFF_BACKMODE + 1),
                  (__bit)(settings_shape == 1));
    if (settings_shape == 0xFF) {
        if (block_ok()) {
            settings_shape = 0;
        } else {
            ee_write_page(EE_BASE, (UINT8)(EE_OFF_BACKMODE + 1), 1);
            settings_shape = 1;
        }
    }
}

/* Snapshot of the stored values, taken after a load and after every save.
 * settings_task() compares it with the live values so a change made by a Fn
 * combo (which never calls settings_touch) still marks the block dirty. */
static void shadow_sync(void)
{
    UINT8 i;

    shadow[0] = led_effect;
    shadow[1] = led_brightness;
    shadow[2] = led_speed;
    shadow[3] = led_back;
    for (i = 0; i < 8; i++) {
        shadow[4 + i] = led_gain[i];
    }
    for (i = 0; i < 3; i++) {
        shadow[12 + i] = led_cal[i];
    }
    shadow[15] = led_back_level;
    for (i = 0; i < 3; i++) {
        shadow[16 + i] = led_back_rgb[i];
    }
    shadow[19] = kbd_debounce;
    shadow[20] = kbd_gaming;
    shadow[21] = led_boot_mode;
    shadow[22] = led_boot_pace;
    shadow[23] = led_back_mode;
}

/* ---- remap block ------------------------------------------------------ */

static UINT8 map_checksum(void)
{
    UINT8 i, s = (UINT8)(MAP_MAGIC0 ^ MAP_MAGIC1 ^ MAP_VER);

    for (i = 0; i < K_COUNT; i++) {
        s = (UINT8)(s ^ keymap_override[i]);
    }
    return s;
}

static UINT8 map_checksum_block(void)
{
    return ee_sum(MAP_BASE + 4, K_COUNT) ^ MAP_MAGIC0 ^ MAP_MAGIC1 ^ MAP_VER;
}

static __bit map_ok(void)
{
    if (ee_read(MAP_BASE) != MAP_MAGIC0 || ee_read(MAP_BASE + 1) != MAP_MAGIC1) {
        return 0;
    }
    if (ee_read(MAP_BASE + 2) != MAP_VER) {
        return 0;
    }
    return (ee_read(MAP_BASE + 3) == map_checksum_block()) ? 1 : 0;
}

static void write_map(void)
{
    UINT8 i;

    /* Page 0 holds the header and the first 60 entries ... */
    for (i = 0; i < EE_BLOCK; i++) {
        ee_page[i] = 0;
    }
    ee_page[0] = MAP_MAGIC0;
    ee_page[1] = MAP_MAGIC1;
    ee_page[2] = MAP_VER;
    ee_page[3] = map_checksum();
    for (i = 0; i < 60; i++) {
        ee_page[4 + i] = keymap_override[i];
    }
    ee_write_page(MAP_BASE, EE_BLOCK, (__bit)(settings_shape == 1));

    /* ... and page 1 the remaining three. */
    for (i = 0; i < EE_BLOCK; i++) {
        ee_page[i] = 0;
    }
    for (i = 60; i < K_COUNT; i++) {
        ee_page[i - 60] = keymap_override[i];
    }
    ee_write_page((UINT16)(MAP_BASE + EE_BLOCK), (UINT8)(K_COUNT - 60),
                  (__bit)(settings_shape == 1));
}

/* ---- custom picture block -------------------------------------------- */

static UINT8 cus_checksum(void)
{
    UINT8 i, s = CUS_VER;

    for (i = 0; i < CUS_BYTES; i++) {
        s = (UINT8)(s ^ ((__xdata UINT8 *)led_custom)[i]);
    }
    return s;
}

static UINT8 cus_checksum_block(void)
{
    return ee_sum(CUS_BASE + 3, CUS_BYTES) ^ CUS_VER;
}

static __bit custom_ok(void)
{
    if (ee_read(CUS_BASE) != 0xC5 || ee_read(CUS_BASE + 1) != CUS_VER) {
        return 0;
    }
    return (ee_read(CUS_BASE + 2) == cus_checksum_block()) ? 1 : 0;
}

static void write_custom(void)
{
    UINT8 page, i;
    UINT16 used = (UINT16)(3 + CUS_BYTES);

    for (page = 0; page < 3; page++) {
        UINT8 n = (UINT8)(used - (UINT16)page * EE_BLOCK);

        if (n > EE_BLOCK) {
            n = EE_BLOCK;
        }
        for (i = 0; i < EE_BLOCK; i++) {
            ee_page[i] = 0;
        }
        stage(page, 0, 0xC5);
        stage(page, 1, CUS_VER);
        stage(page, 2, cus_checksum());
        for (i = 0; i < CUS_BYTES; i++) {
            stage(page, (UINT16)(3 + i), ((__xdata UINT8 *)led_custom)[i]);
        }
        ee_write_page((UINT16)(CUS_BASE + (UINT16)page * EE_BLOCK), n,
                      (__bit)(settings_shape == 1));
    }
}

/* ---- macro block ------------------------------------------------------ */

static UINT8 mac_checksum(void)
{
    UINT8 i, j, s = (UINT8)(MAC_MAGIC ^ MAC_VER);

    for (i = 0; i < MACRO_SLOTS; i++) {
        s = (UINT8)(s ^ macro_key[i] ^ macro_len[i]);
        for (j = 0; j < MACRO_STEPS; j++) {
            s = (UINT8)(s ^ macro_steps[i][j].usage);
            s = (UINT8)(s ^ macro_steps[i][j].hold);
        }
    }
    return s;
}

static UINT8 mac_checksum_block(void)
{
    return ee_sum(MAC_BASE + 3, MAC_PAYLOAD) ^ MAC_MAGIC ^ MAC_VER;
}

static __bit mac_ok(void)
{
    if (ee_read(MAC_BASE) != MAC_MAGIC || ee_read(MAC_BASE + 1) != MAC_VER) {
        return 0;
    }
    return (ee_read(MAC_BASE + 2) == mac_checksum_block()) ? 1 : 0;
}

static void write_mac(void)
{
    UINT8 page, i, j;
    UINT16 a;
    UINT16 used = (UINT16)(3 + MAC_PAYLOAD);

    for (page = 0; page < MAC_BLOCKS; page++) {
        UINT8 n = (UINT8)(used - (UINT16)page * EE_BLOCK);

        if (n > EE_BLOCK) {
            n = EE_BLOCK;
        }
        for (i = 0; i < EE_BLOCK; i++) {
            ee_page[i] = 0;
        }
        stage(page, 0, MAC_MAGIC);
        stage(page, 1, MAC_VER);
        stage(page, 2, mac_checksum());
        a = 3;
        for (i = 0; i < MACRO_SLOTS; i++) {
            stage(page, a++, macro_key[i]);
            stage(page, a++, macro_len[i]);
            for (j = 0; j < MACRO_STEPS; j++) {
                stage(page, a++, macro_steps[i][j].usage);
                stage(page, a++, macro_steps[i][j].hold);
            }
        }
        ee_write_page((UINT16)(MAC_BASE + (UINT16)page * EE_BLOCK), n,
                      (__bit)(settings_shape == 1));
    }
}

static void load_macros(void)
{
    UINT8 i, j;
    UINT16 a = (UINT16)(MAC_BASE + 3);

    for (i = 0; i < MACRO_SLOTS; i++) {
        macro_key[i] = ee_read(a++);
        macro_len[i] = ee_read(a++);
        if (macro_len[i] > MACRO_STEPS) {
            macro_len[i] = MACRO_STEPS;
        }
        for (j = 0; j < MACRO_STEPS; j++) {
            macro_steps[i][j].usage = ee_read(a++);
            macro_steps[i][j].hold = ee_read(a++);
        }
    }
}

/* ---- flash self-test -------------------------------------------------- */

/* Scratch pages, all inside the unused tail of the data flash.  0xF000 is the
 * live settings block and is never erased by the self-test, so the board keeps
 * its look while the diagnostics run. */
#define ST_PAGE_LIVE     0xF000
#define ST_PAGE_BLOCK    0xF2C0
#define ST_PAGE_OFF      0xF340
#define ST_PAGE_BYTE     0xF380
#define ST_PAGE_MARK     0xF300     /* a page the ladder has written before */
#define ST_VAL           0xA5
#define FLASH_TEST_LEN   48

static __xdata UINT8 st_out[FLASH_TEST_LEN];
static __xdata UINT8 st_n;
static __xdata UINT8 st_buf[64];    /* source for the block-program test   */
static __xdata UINT8 st_cnt;
static __xdata UINT8 st_first;
static __xdata UINT8 st_val;

static void st_put(UINT8 v)
{
    if (st_n < FLASH_TEST_LEN) {
        st_out[st_n++] = v;
    }
}

/* How many bytes of a 64-byte page are non-zero, where the first one is and
 * what it holds.  Used after every program attempt so a byte written to the
 * wrong offset is still found. */
static void st_scan(UINT16 page)
{
    UINT8 i, c = 0, f = 0xFF, v = 0;

    for (i = 0; i < 64; i++) {
        UINT8 b = ee_read((UINT16)(page + i));

        if (b != 0) {
            if (c == 0) {
                f = i;
                v = b;
            }
            c++;
        }
    }
    st_cnt = c;
    st_first = f;
    st_val = v;
}

static void st_scan_put(void)
{
    st_put(st_cnt);
    st_put(st_first);
    st_put(st_val);
}

/* Erase one page, then program ST_VAL into it once:
 *   off        byte offset inside the page
 *   bufmod     written straight into ROM_BUF_MOD
 *   use_dptr   1 = block mode, data comes from st_buf in xdata
 *   we         1 = the masked save-path enable, else written to GLOBAL_CFG
 * The page is not read before the program, so the first read after it cannot
 * be served from a stale flash cache.  Records cfg, erase status, program
 * status and the page scan. */
static void st_prog(UINT16 page, UINT8 off, UINT8 bufmod,
                    UINT8 use_dptr, UINT8 we)
{
    EA = 0;
    if (we == 1) {
        ee_write_enable(1);
    } else {
        SAFE_MOD = 0x55;
        SAFE_MOD = 0xAA;
        GLOBAL_CFG = we;
    }
    ROM_ADDR_L = (UINT8)(page & 0xFF);
    ROM_ADDR_H = (UINT8)(page >> 8);
    ROM_BUF_MOD = bROM_BUF_BYTE;
    st_put((UINT8)GLOBAL_CFG);
    ROM_DAT_BUF = 0x00;
    ROM_CTRL = ROM_CMD_ERASE;
    st_put((UINT8)ROM_STATUS);

    ROM_ADDR_L = (UINT8)((page + off) & 0xFF);
    ROM_ADDR_H = (UINT8)((page + off) >> 8);
    ROM_BUF_MOD = bufmod;
    if (use_dptr) {
        st_buf[0] = ST_VAL;
        DPL = (UINT8)(UINT16)st_buf;
        DPH = (UINT8)((UINT16)st_buf >> 8);
    } else {
        ROM_DAT_BUF = ST_VAL;
    }
    ROM_CTRL = ROM_CMD_PROG;
    st_put((UINT8)ROM_STATUS);

    ee_write_enable(0);
    EA = 1;

    st_scan(page);
    st_scan_put();
}

/* Erase-only test on a page whose contents are known to be non-zero. */
static void st_erase_only(UINT16 page)
{
    EA = 0;
    ee_write_enable(1);
    ROM_ADDR_L = (UINT8)(page & 0xFF);
    ROM_ADDR_H = (UINT8)(page >> 8);
    ROM_BUF_MOD = bROM_BUF_BYTE;
    st_put((UINT8)GLOBAL_CFG);
    ROM_DAT_BUF = 0x00;
    ROM_CTRL = ROM_CMD_ERASE;
    st_put((UINT8)ROM_STATUS);
    st_put(0x00);                       /* placeholder, keeps the layout */
    ee_write_enable(0);
    EA = 1;

    st_scan(page);
    st_scan_put();
}

/* The production writer itself: erase a page and program three bytes into it.
 * If this variant lands, the save path lands, because it is the same code. */
static void st_page(UINT16 page, __bit each)
{
    UINT8 i;

    for (i = 0; i < EE_BLOCK; i++) {
        ee_page[i] = 0;
    }
    ee_page[0x11] = ST_VAL;
    ee_page[0x12] = (UINT8)(ST_VAL ^ 0xFF);
    ee_page[0x13] = 0x5A;

    st_put((UINT8)GLOBAL_CFG);          /* write window open? */
    ee_write_page(page, 0x14, each);
    st_put(settings_rom_status);
    st_put((UINT8)GLOBAL_CFG);          /* back to baseline */

    st_scan(page);
    st_scan_put();
}

/* Fills `out` with the self-test record; returns the number of bytes. */
UINT8 settings_selftest(UINT8 *out)
{
    UINT16 a, c = 0, f = 0xFFFF;
    UINT8 v = 0, i;

    st_n = 0;
    st_put(0xA5);                       /* record marker */
    st_put((UINT8)GLOBAL_CFG);          /* baseline, outside safe mode */

    /* Whole data flash: how much is non-zero and where it starts. */
    for (a = 0; a < 1024; a++) {
        UINT8 b = ee_read((UINT16)(ST_PAGE_LIVE + a));

        if (b != 0) {
            if (c == 0) {
                f = a;
                v = b;
            }
            c++;
        }
    }
    st_put((UINT8)(c > 255 ? 255 : c));
    st_put((UINT8)(f & 0xFF));
    st_put((UINT8)(f >> 8));
    st_put(v);

    /* The settings block, before anything can erase it. */
    for (i = 0; i < 4; i++) {
        st_put(ee_read(ST_PAGE_LIVE + i));
    }

    /* Byte program at offset 0, the plain datasheet sequence. */
    st_prog(ST_PAGE_BYTE, 0x00, bROM_BUF_BYTE, 0, 1);
    /* Byte program at a non-zero offset. */
    st_prog(ST_PAGE_OFF, 0x25, bROM_BUF_BYTE, 0, 1);
    /* Erase a page on its own.  Never the live settings block: that one has
     * to survive so the board keeps its look while the diagnostics run. */
    st_erase_only(ST_PAGE_BLOCK);
    /* And the production writer, in both shapes. */
    st_page(ST_PAGE_MARK, 0);
    st_page(ST_PAGE_BYTE, 1);

    st_put((UINT8)GLOBAL_CFG);          /* must be back to baseline */

    for (i = 0; i < st_n; i++) {
        out[i] = st_out[i];
    }
    return st_n;
}

/* ---- public ----------------------------------------------------------- */

/* Read one byte of the data flash through MOVC.  Used by the host to look at
 * the stored blocks without touching them. */
UINT8 settings_flash_read(UINT16 addr)
{
    return ee_read(addr);
}

void settings_load(void)
{
    UINT8 i;

    settings_block_valid = block_ok() ? 1 : 0;
    if (!settings_block_valid) {
        /* First boot, a block from an older firmware, or a save that was
         * interrupted.  Keep the compiled defaults and rewrite the block
         * once so the settings have a clean baseline. */
        dirty = 1;
        save_timer = 0;
    } else {
        led_effect = ee_read(EE_BASE + EE_OFF_EFFECT);
        if (led_effect >= LED_FX_USER_COUNT) {
            led_effect = LED_FX_RAINBOW;
        }
        led_brightness = ee_read(EE_BASE + EE_OFF_BRIGHT);
        if (led_brightness > 127) {
            led_brightness = 80;
        }
        led_speed = ee_read(EE_BASE + EE_OFF_SPEED);
        if (led_speed < 1 || led_speed > 8) {
            led_speed = 4;
        }
        led_back = ee_read(EE_BASE + EE_OFF_BACK) ? 1 : 0;
        for (i = 0; i < 8; i++) {
            led_gain[i] = ee_read(EE_BASE + EE_OFF_GAIN + i);
        }
        for (i = 0; i < 3; i++) {
            led_cal[i] = ee_read(EE_BASE + EE_OFF_CAL + i);
        }
        led_back_level = ee_read(EE_BASE + EE_OFF_BACKLVL);
        if (led_back_level > 127) {
            led_back_level = 127;
        }
        for (i = 0; i < 3; i++) {
            led_back_rgb[i] = ee_read(EE_BASE + EE_OFF_BACKRGB + i);
        }
        kbd_debounce = ee_read(EE_BASE + EE_OFF_DEBOUNCE);
        if (kbd_debounce < 1 || kbd_debounce > 6) {
            kbd_debounce = 2;
        }
        kbd_gaming = ee_read(EE_BASE + EE_OFF_GAMING) ? 1 : 0;
        if (legacy_block) {
            /* Version 4 had no start-up or rear animation.  Keep every value
             * the user already had and use the compiled defaults for the new
             * ones, then let the save tick rewrite the block as version 5. */
            led_boot_mode = LED_BOOT_SWEEP;
            led_boot_pace = 1;
            led_back_mode = LED_BACK_SOLID;
            dirty = 1;
            save_timer = SAVE_DELAY_MS;
        } else {
            led_boot_mode = ee_read(EE_BASE + EE_OFF_BOOTMODE);
            if (led_boot_mode >= LED_BOOT_COUNT) {
                led_boot_mode = LED_BOOT_SWEEP;
            }
            led_boot_pace = ee_read(EE_BASE + EE_OFF_BOOTPACE);
            if (led_boot_pace > 2) {
                led_boot_pace = 1;
            }
            led_back_mode = ee_read(EE_BASE + EE_OFF_BACKMODE);
            if (led_back_mode >= LED_BACK_COUNT) {
                led_back_mode = LED_BACK_SOLID;
            }
        }
    }

    if (map_ok()) {
        for (i = 0; i < K_COUNT; i++) {
            keymap_override[i] = ee_read(MAP_BASE + 4 + i);
        }
    } else {
        keymap_reset_overrides();
        dirty_map = 1;
    }

    if (custom_ok()) {
        for (i = 0; i < CUS_BYTES; i++) {
            ((__xdata UINT8 *)led_custom)[i] = ee_read(CUS_BASE + 3 + i);
        }
    } else {
        for (i = 0; i < CUS_BYTES; i++) {
            ((__xdata UINT8 *)led_custom)[i] = 0;
        }
        dirty_custom = 1;
    }

    if (mac_ok()) {
        load_macros();
    } else {
        macro_reset();
        dirty_macro = 1;
    }

    shadow_sync();
}

/* Bitmask of the sections that are queued.  For the diagnostics page. */
UINT8 settings_flags(void)
{
    UINT8 f = 0;

    if (dirty) {
        f |= 0x01;
    }
    if (dirty_map) {
        f |= 0x02;
    }
    if (dirty_custom) {
        f |= 0x04;
    }
    if (dirty_macro) {
        f |= 0x08;
    }
    return f;
}

/* Ticks left on the save debounce.  One tick is one settings_task() call. */
UINT16 settings_save_timer(void)
{
    return save_timer;
}

/* Write the settings block immediately and report whether it verified.  Lets
 * the host exercise the save path without waiting out the debounce timer. */
UINT8 settings_force_save(void)
{
    save_timer = 0;
    write_block();
    if (block_ok()) {
        dirty = 0;
        settings_block_valid = 1;
        shadow_sync();
        if (settings_writes != 0xFF) {
            settings_writes++;
        }
        return 1;
    }
    dirty = 1;
    return 0;
}

void settings_touch(void)
{
    dirty = 1;
    save_timer = SAVE_DELAY_MS;
}

void settings_touch_map(void)
{
    dirty_map = 1;
    save_timer = SAVE_DELAY_MS;
}

void settings_touch_custom(void)
{
    dirty_custom = 1;
    save_timer = SAVE_DELAY_MS;
}

void settings_touch_macro(void)
{
    dirty_macro = 1;
    save_timer = SAVE_DELAY_MS;
}

void settings_sync(void)
{
    /* Adopt the values currently in force without writing the flash.  Used
     * when the host changes brightness through OpenRGB: those changes are
     * driven by a slider and must not wear out the data flash, so they are
     * only persisted when OpenRGB asks for it (the save flag). */
    shadow_sync();
}

/* One pass of the save loop: write every queued section, read each one back
 * and try again if a byte did not take.  Returns 1 when everything landed.
 * Shared by the debounced task and by the host-driven "save now" command so
 * both report the same truth. */
static __bit save_sections(void)
{
    UINT8 attempt;

    for (attempt = 0; attempt < SAVE_TRIES; attempt++) {
        if (settings_rounds != 0xFF) {
            settings_rounds++;
        }
        if (dirty) {
            write_block();
            if (block_ok()) {
                dirty = 0;
                settings_block_valid = 1;
            }
        }
        if (dirty_map) {
            write_map();
            if (map_ok()) {
                dirty_map = 0;
            }
        }
        if (dirty_custom) {
            write_custom();
            if (custom_ok()) {
                dirty_custom = 0;
            }
        }
        if (dirty_macro) {
            write_mac();
            if (mac_ok()) {
                dirty_macro = 0;
            }
        }
        if (!dirty && !dirty_map && !dirty_custom && !dirty_macro) {
            /* The live values are now the stored ones.  Without this the
             * change detector compared them against the stale snapshot on the
             * next tick and saved the same block a second time 1.5 s later,
             * doubling the erase cycles of every host config write. */
            shadow_sync();
            return 1;
        }
    }
    return 0;
}

void settings_task(void)
{
    UINT8 i;

    /* Detect a change made by the Fn combos or by the host. */
    for (i = 0; i < SHADOW_LEN; i++) {
        UINT8 now;

        switch (i) {
        case 0:  now = led_effect;      break;
        case 1:  now = led_brightness;  break;
        case 2:  now = led_speed;       break;
        case 3:  now = led_back;        break;
        case 15: now = led_back_level;  break;
        case 19: now = kbd_debounce;    break;
        case 20: now = kbd_gaming;      break;
        case 21: now = led_boot_mode;   break;
        case 22: now = led_boot_pace;   break;
        case 23: now = led_back_mode;   break;
        default:
            if (i >= 4 && i < 12) {
                now = led_gain[i - 4];
            } else if (i >= 12 && i < 15) {
                now = led_cal[i - 12];
            } else {
                now = led_back_rgb[i - 16];
            }
            break;
        }
        if (shadow[i] != now) {
            shadow_sync();
            /* Only arm the debounce when nothing is queued yet.  Re-arming it
             * on every tick let a value that changes faster than the delay
             * postpone the save for ever, so a change could be made and never
             * reach the flash. */
            if (!dirty && !dirty_map && !dirty_custom && !dirty_macro) {
                save_timer = SAVE_DELAY_MS;
            }
            if (settings_shadow_hits != 0xFF) {
                settings_shadow_hits++;
            }
            dirty = 1;
            return;
        }
    }

    if (!dirty && !dirty_map && !dirty_custom && !dirty_macro) {
        return;
    }
    if (save_timer) {
        save_timer--;
        return;
    }

    /* A flash write stalls the main loop for several milliseconds.  Matrix
     * frames sampled during the stall are overwritten before they can be
     * decoded, so a keystroke typed at that moment would disappear.  Wait for
     * a gap with nothing held; the 2 s cap keeps a key that is held down (or a
     * stuck switch) from postponing the save for ever. */
    if (keyboard_any_down() && save_hold < 2000) {
        save_hold++;
        return;
    }
    save_hold = 0;

    if (save_sections()) {
        if (settings_writes != 0xFF) {
            settings_writes++;
        }
    } else {
        /* A section that still does not verify means a bad flash cell or a
         * dead block.  Pace the retries instead of hammering the flash:
         * without this the very next 1 ms tick tried again, which kept the CPU
         * in the ROM controller and made the panel flicker continuously. */
        save_timer = SAVE_DELAY_MS * 4;
    }
}

/* Host-driven "save everything" (CMD_SAVE_ALL).  Unlike the debounced path it
 * does not wait for a typing gap, because the host is blocked on the reply and
 * is not typing.  The reply now means the bytes verified on the flash, not
 * merely that a save was queued for later. */
UINT8 settings_save_all_now(void)
{
    save_timer = 0;
    save_hold = 0;
    if (!save_sections()) {
        save_timer = SAVE_DELAY_MS * 4;
        return 0;
    }
    if (settings_writes != 0xFF) {
        settings_writes++;
    }
    return 1;
}
