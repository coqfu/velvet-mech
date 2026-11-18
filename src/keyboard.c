#include "ch555.h"
#include "board.h"
#include "usb.h"
#include "keymap.h"
#include "led.h"
#include "macro.h"
#include "settings.h"
#ifdef OPENRGB
#include "openrgb.h"
#endif

/*
 * Matrix scan.
 *
 * Columns are P0.0..P0.7 (col 0..7) and P3.0..P3.5 (col 8..13); rows are
 * P4.1..P4.5.  The board's diodes conduct from column to row, so a column is
 * driven low and P4 is sampled: a zero bit means the switch at that
 * column/row is closed.  Driving a row and reading a column never works.
 *
 * While the RGB LED engine runs, P0/P3/P4 belong to it, so the columns are
 * sampled one per LED inhibition slot (see led.c).  This is also why the
 * debounce is done per key with one sample per LED frame.
 */

#define REPORT_LEN       16    /* NKRO: mods + pad + 112 key bits */
#define BOOT_LEN         8

volatile __bit kbd_dirty = 0;

/* Host-tunable input behaviour.  Debounce counts complete matrix frames. */
UINT8 kbd_debounce = 2;
UINT8 kbd_gaming = 0;

static __xdata UINT8 k_state[K_COUNT];
static __xdata UINT8 k_count[K_COUNT];      /* consecutive agreeing frames */
static __xdata UINT8 m_col[MATRIX_COLS];    /* raw samples, written by the ISR */
static __xdata UINT8 m_snap[MATRIX_COLS];   /* last complete frame            */
volatile __xdata UINT8 frame_seq = 0;       /* bumped when m_snap is republished */

static __xdata UINT8 rep_nk[REPORT_LEN];
static __xdata UINT8 rep_boot[BOOT_LEN];
static __xdata UINT8 rep_media[2];

static __xdata UINT8 prev_nk[REPORT_LEN];
static __xdata UINT8 prev_boot[BOOT_LEN];
static __xdata UINT8 prev_media[2];

static __xdata UINT8 was_ready = 0;
static __xdata UINT8 proto_prev = 0xFF;     /* cached report layout */

static void cols_idle(void)
{
    P0 = 0xFF;
    P3 = 0xFF;
}

static void col_drive(UINT8 col)
{
    if (col < 8) {
        P0 = (UINT8)~(1 << col);
    } else {
        P3 = (UINT8)~(1 << (col - 8));
    }
}

static void settle(void)
{
    UINT8 i = 40;
    while (i--) {
        _nop_();
    }
}

void keyboard_init(void)
{
    UINT8 i;

    P4_LED_KEY = 0x00;      /* P4 as ordinary GPIO, not LED current mode */
    LED_CTRL = 0x00;        /* LED engine off so it cannot drive pins     */
    LED_PWM_OE = 0x00;
    LED_COMMON = 0x00;

    P0_MOD_OC = 0xFF; P0_DIR_PU = 0xFF;
    P1_MOD_OC = 0xFF; P1_DIR_PU = 0xFF;
    P2_MOD_OC = 0xFF; P2_DIR_PU = 0xFF;
    P3_MOD_OC = 0xFF; P3_DIR_PU = 0xFF;
    P4_MOD_OC = 0xFF; P4_DIR_PU = 0xFF;

    cols_idle();
    P4 = 0xFF;

    for (i = 0; i < K_COUNT; i++) {
        k_state[i] = 0;
        k_count[i] = 0;
    }
    keymap_reset_overrides();
    for (i = 0; i < MATRIX_COLS; i++) {
        m_col[i] = 0xFF;
        m_snap[i] = 0xFF;
    }
    frame_seq = 0;
    for (i = 0; i < REPORT_LEN; i++) {
        prev_nk[i] = 0;
    }
    for (i = 0; i < BOOT_LEN; i++) {
        prev_boot[i] = 0;
    }
    prev_media[0] = 0;
    prev_media[1] = 0;

    settle();
}

/* ---- LED-driven scan (one column per inhibition slot) ---------------- */

/* The ISR only stores the raw P4 byte for the column it sampled; decoding and
 * debouncing happen in keyboard_process() so the inhibition slot stays short. */
void keyboard_isr_col(UINT8 col, UINT8 p4) __reentrant
{
    if (col >= MATRIX_COLS) {
        return;
    }
    m_col[col] = p4;
}

/* Called from the LED ISR when the common wraps, i.e. exactly when a complete
 * set of 14 columns has just been sampled.  Publishing the whole frame at once
 * stops the main loop from decoding a mix of the old and the new frame, which
 * used to show up as keys appearing, vanishing or sticking at random.  The
 * sequence number lets the main loop consume each frame exactly once: a flag
 * could be cleared and then re-raised by the ISR during the decode, which made
 * the same frame advance the debounce twice. */
void keyboard_frame_end(void)
{
    UINT8 i;

    for (i = 0; i < MATRIX_COLS; i++) {
        m_snap[i] = m_col[i];
    }
    frame_seq++;
}

/* Decode one complete matrix frame, debounce, update the key state.
 *
 * A matrix frame is sampled once per LED frame, ~89 per second (11.2 ms), so
 * the default kbd_debounce of 2 means a press is accepted after two matching
 * frames (about 22 ms) and a release after two open ones.  Accepting the very
 * first noisy sample, as the old code did, is what made single phantom keys
 * show up while typing. */
UINT8 keyboard_process(void)
{
    UINT8 col, row, i;
    static __xdata UINT8 tmp[MATRIX_COLS];
    UINT8 seq;

    /* Backstop for a zero debounce.  Both the press and the release branch
     * would match on the same frame, so a held key would toggle on every
     * matrix frame and the host would see a key-repeat storm.  The settings
     * loader and 0x79 already clamp to 1..6; this cannot hurt. */
    if (kbd_debounce < 1) {
        kbd_debounce = 1;
    }

    /* seqlock: the ISR may republish m_snap while we copy it */
    do {
        seq = frame_seq;
        for (i = 0; i < MATRIX_COLS; i++) {
            tmp[i] = m_snap[i];
        }
    } while (seq != frame_seq);

    for (col = 0; col < MATRIX_COLS; col++) {
        UINT8 now = tmp[col];

        for (row = 0; row < 5; row++) {
            UINT8 k = keymap_rowkey(col, row);
            UINT8 bit = (UINT8)(1 << (row + 1));
            UINT8 raw_down;

            if (k == 0xFF) {
                continue;
            }
            raw_down = ((now & bit) == 0) ? 1 : 0;

            /* Integrator instead of a fixed two-frame compare: the key is
             * pressed once the counter has filled and released only when it
             * has drained, so the host can trade latency against chatter.
             * kbd_debounce == 1 gives one frame, i.e. about 11 ms.
             *
             * The counter saturates at kbd_debounce rather than at a fixed
             * ceiling.  It used to climb to 250 while a key was held, and the
             * release path only fires when the counter reaches zero, so a key
             * that had been held stayed logically down for as long as it was
             * held: hold Backspace for half a second and it took another half
             * second to let go.  That is what made typing feel laggy, swallow
             * the next press of the same key and keep auto-repeating. */
            if (raw_down) {
                if (k_count[k] < kbd_debounce) {
                    k_count[k]++;
                }
            } else if (k_count[k]) {
                k_count[k]--;
            }

            /* Lowering the debounce from the app must not leave a held key
             * waiting to drain a count that no longer applies. */
            if (k_count[k] > kbd_debounce) {
                k_count[k] = kbd_debounce;
            }

            if (!k_state[k] && k_count[k] >= kbd_debounce) {
                k_state[k] = 1;
                kbd_dirty = 1;
                led_key_event(col, row);
                macro_key_event(k);
            } else if (k_state[k] && k_count[k] == 0) {
                k_state[k] = 0;
                kbd_dirty = 1;
            }
        }
    }

    return seq;
}

/* True while any key is logically down.  settings_task() uses it to keep a
 * flash write out of the middle of typing. */
UINT8 keyboard_any_down(void)
{
    UINT8 k;

    /* A macro keeps the board busy the same way a held key does, and a flash
     * write stalls macro_task() for the duration.  The 2 s cap in
     * settings_task() still stops a long macro from postponing the save for
     * ever. */
    if (macro_is_active()) {
        return 1;
    }
    for (k = 0; k < K_COUNT; k++) {
        if (k_state[k]) {
            return 1;
        }
    }
    return 0;
}

/* ---- fallback scan when the LED engine is not running ---------------- */

void keyboard_scan_gpio(void)
{
    UINT8 col;

    for (col = 0; col < MATRIX_COLS; col++) {
        cols_idle();
        settle();
        col_drive(col);
        settle();
        m_col[col] = P4;
    }
    cols_idle();

    keyboard_frame_end();
    keyboard_process();
}

/* ---- report composition ---------------------------------------------- */

static UINT8 same(const UINT8 *a, const UINT8 *b, UINT8 len)
{
    UINT8 i;
    for (i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

void keyboard_report(void)
{
    UINT8 k, i;
    UINT8 mods = 0;
    UINT8 layer;
    UINT16 media = 0;
    UINT8 mode;
    UINT8 pending;
    UINT8 macro_u = macro_usage();

    if (!usb_ready) {
        was_ready = 0;
        return;
    }
    if (usb_reconfigured) {
        /* The host configured the device again and starts with an empty idea
         * of which keys are down, so the cached report is not comparable any
         * more.  Force the next send to carry the full state. */
        usb_reconfigured = 0;
        was_ready = 0;
    }
    if (!was_ready) {
        /* freshly configured: force a full state report */
        for (i = 0; i < REPORT_LEN; i++) prev_nk[i] = 0xFF;
        for (i = 0; i < BOOT_LEN; i++) prev_boot[i] = 0xFF;
        prev_media[0] = 0xFF;
        prev_media[1] = 0xFF;
        was_ready = 1;
    }

    layer = k_state[K_FN] ? 1 : 0;

    for (i = 0; i < REPORT_LEN; i++) {
        rep_nk[i] = 0;
    }

    for (k = 0; k < K_COUNT; k++) {
        UINT8 u;
        if (!k_state[k] || k == K_FN) {
            continue;
        }
        /* A key that fires a macro types nothing by itself; the macro's
         * usages enter the report a few lines below.  Keying off the binding
         * rather than "is a macro playing right now" also silences the key
         * when its macro has already finished but the switch is still held. */
        if (macro_bound(k)) {
            continue;
        }
        if (media == 0) {
            media = keymap_media(layer, k);
        }
        u = keymap_usage(layer, k);
        if (u == 0) {
            continue;
        }
        if (kbd_gaming && u == 0xE3) {
            continue;           /* Win key masked while gaming mode is on */
        }
        /* The NKRO bitmap only covers usages 0x00..0x6F and the modifier byte
         * only 0xE0..0xE7.  A remap or macro byte outside those ranges used to
         * be written at face value: 0x70..0xDF ran past the end of rep_nk[]
         * and overwrote the cached report and the media report, which made
         * `same()` compare garbage and could hold a key down or replay the
         * wrong one.  Drop anything the descriptor cannot express instead. */
        if (u >= 0xE0 && u <= 0xE7) {
            mods |= (UINT8)(1 << (u - 0xE0));
        } else if (u < 0x70) {
            rep_nk[2 + (u >> 3)] |= (UINT8)(1 << (u & 7));
        }
    }
    if (macro_u) {
        /* Macro playback goes through the same bitmap as a real key, so it
         * combines with whatever the user is holding. */
        if (macro_u >= 0xE0 && macro_u <= 0xE7) {
            mods |= (UINT8)(1 << (macro_u - 0xE0));
        } else if (macro_u < 0x70) {
            rep_nk[2 + (macro_u >> 3)] |= (UINT8)(1 << (macro_u & 7));
        }
    }
    rep_nk[0] = mods;

    /* boot-protocol fallback: modifier byte + up to six usages */
    rep_boot[0] = mods;
    rep_boot[1] = 0;
    for (i = 2; i < BOOT_LEN; i++) {
        rep_boot[i] = 0;
    }
    i = 2;
    for (k = 4; k < 0x70 && i < BOOT_LEN; k++) {
        if (rep_nk[2 + (k >> 3)] & (1 << (k & 7))) {
            rep_boot[i++] = k;
        }
    }

    rep_media[0] = (UINT8)(media & 0xFF);
    rep_media[1] = (UINT8)(media >> 8);

    mode = usb_boot_proto ? 0 : 1;

    /* A protocol switch changes the report layout, so the cached copy belongs
     * to the other layout.  Re-send the current state instead of comparing
     * against it: a key held across the switch was otherwise never announced
     * in the new layout and the host kept it down. */
    if (mode != proto_prev) {
        proto_prev = mode;
        for (i = 0; i < REPORT_LEN; i++) prev_nk[i] = 0xFF;
        for (i = 0; i < BOOT_LEN; i++) prev_boot[i] = 0xFF;
    }

    pending = 0;

    if (mode) {
        if (!same(rep_nk, prev_nk, REPORT_LEN)) {
            if (!usb_kbd_busy()) {
                usb_kbd_send(rep_nk, REPORT_LEN);
                for (i = 0; i < REPORT_LEN; i++) prev_nk[i] = rep_nk[i];
            } else {
                pending = 1;
            }
        }
    } else {
        if (!same(rep_boot, prev_boot, BOOT_LEN)) {
            if (!usb_kbd_busy()) {
                usb_kbd_send(rep_boot, BOOT_LEN);
                for (i = 0; i < BOOT_LEN; i++) prev_boot[i] = rep_boot[i];
            } else {
                pending = 1;
            }
        }
    }

    if (!same(rep_media, prev_media, 2)) {
        if (!usb_consumer_busy()) {
            usb_consumer_send(media);
            prev_media[0] = rep_media[0];
            prev_media[1] = rep_media[1];
        } else {
            pending = 1;
        }
    }

    /* Keep kbd_dirty set while an endpoint was busy.  The main loop then tries
     * again on the next 1 ms tick instead of waiting for a slower pass, so a
     * release is not held back just because the endpoint happened to be busy
     * at that instant. */
    if (!pending) {
        kbd_dirty = 0;
    }
}

/* ---- on-keyboard control (Fn + \ [ ] and arrows) ---------------------- */

void keyboard_special(void)
{
    static UINT8 prev = 0;
    UINT8 now = 0;
    UINT8 trimmed = 0;

    if (!k_state[K_FN]) {
        prev = 0;
        return;
    }

    if (k_state[K_LBRC]) now |= 0x01;
    if (k_state[K_RBRC]) now |= 0x02;
    if (k_state[K_BSLS]) now |= 0x04;
    if (k_state[K_UP])   now |= 0x08;
    if (k_state[K_DOWN]) now |= 0x10;
    if (k_state[K_LEFT]) now |= 0x20;
    if (k_state[K_RGHT]) now |= 0x40;
    if (k_state[K_B])    now |= 0x80;

#ifdef OPENRGB
    /* Any lighting control on the keyboard takes over from the host, so the
     * effect keys keep working while OpenRGB is connected. */
    if (now && !prev) {
        openrgb_leave_direct();
    }
#endif

    /* Effect selection: Fn + \ cycles forward like most gaming keyboards,
     * Fn + [ and Fn + ] step back and forward. */
    if ((now & 0x01) && !(prev & 0x01)) led_prev_effect();
    if ((now & 0x02) && !(prev & 0x02)) led_next_effect();
    if ((now & 0x04) && !(prev & 0x04)) led_next_effect();

    if ((now & 0x08) && !(prev & 0x08)) {
        if (led_effect == LED_FX_CHAN) {
            led_gain_step(1);
            trimmed = 1;
        } else {
            led_brightness_up();
        }
    }
    if ((now & 0x10) && !(prev & 0x10)) {
        if (led_effect == LED_FX_CHAN) {
            led_gain_step(0);
            trimmed = 1;
        } else {
            led_brightness_down();
        }
    }
    if ((now & 0x20) && !(prev & 0x20)) led_speed_down();
    if ((now & 0x40) && !(prev & 0x40)) led_speed_up();
    if ((now & 0x80) && !(prev & 0x80)) led_back_toggle();

    /* A lighting chord changes the board now, not the stored settings.  Adopt
     * the new values into the save path's change detector so it does not queue
     * a data-flash write behind the press: the erase and program hold the CPU
     * for about 160 ms, and the LED scan stops mid-frame, which looks like the
     * whole keyboard flickering.  Measured: Fn+B marked the block dirty, and
     * the save 1.5 s later froze the frame counter for 0.16 s.
     *
     * The change stays live until the next power cycle.  The host persists it
     * through Apply, which is also when a 160 ms blink is expected.
     *
     * The per-channel trim is the exception: it is a calibration, there is no
     * host path that can write led_gain, and losing it on every replug would
     * mean redoing the measurement.  A trim step therefore still queues the
     * save and takes its one blink. */
    if (now != prev && !trimmed) {
        settings_sync();
    }

    prev = now;
}
