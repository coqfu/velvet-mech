#include "ch555.h"
#include "led.h"
#include "keymap.h"
#include "keyboard.h"
#include "settings.h"

/*
 * Per-key RGB on the CH555 RGB LED controller.
 *
 * The board wires the 63 key LEDs to the controller exactly like the matrix:
 *   common / column line  = P0.0..P0.7 (COM16..23), P3.0..P3.5 (COM24..29)
 *   channel / row line    = P4.1..P4.5 (RED1..5), P2.x GREEN, P1.x BLUE
 *
 * So the LED of the key at (col, row) is driven when common "col" is active
 * and channel "row+1" is on.  One 32-byte record per common:
 *     [intensity, R, G, B] x 8 channels
 * 14 commons x 8 channels x 4 bytes = 448 bytes of xRAM per frame.
 *
 * Exception: the top key row on this board hangs off channels 0, 6 and 7 (the
 * slots nothing else uses) rather than channel 1.  render_col() mirrors row 0
 * into all three so the row follows the key effect instead of the rear colour.
 * Nothing else is wired to those slots on the key commons.
 *
 * The controller streams that record out of xRAM by itself: it snoops the 32
 * bytes read from LED_DMA during every inhibition slot, which is what
 * led_pump() triggers.
 *
 * The interrupt only does what has to happen inside the inhibition window:
 *
 *   slot for common c :  pump record c out of led_buf[show]
 *                        sample matrix column c
 *   wrap to common 16 :  publish led_buf[work] if it is complete
 *
 * The effect renderer runs in the main loop and fills the buffer that is *not*
 * on screen, one whole frame at a time, then raises frame_ready.  The engine
 * picks a frame up only at the common wrap, so the displayed image can never
 * be caught half-drawn.  Because the animation clock advances once per
 * *published* frame, and only a finished frame can be published, the effects
 * cannot tear, cannot skip a step and cannot drift against the panel.
 *
 * Doing this work in the interrupt used to overrun the 42 us inhibition window
 * and, once the main loop stalled for any reason, made the panel look like it
 * was restarting mid-effect.
 *
 * The same inhibition slot is the only moment P4 is not carrying PWM, so the
 * matrix row that shares P4 with the red channel of the active common is
 * sampled here.  That is why key scanning lives in this ISR.
 */

/* The LED engine scans its own commons, which are a superset of the matrix
 * columns: the 63 key LEDs use COM16..COM29 exactly like the matrix, but the
 * back light (led6..led11) hangs off COM30/COM31 (P3.6/P3.7, the SPI1 pins) -
 * two commons nothing used to drive, which is why those LEDs never lit even
 * with every OpenRGB LED set to white.  P3.6/P3.7 are free here: the firmware
 * runs from the internal oscillator and never enables SPI1. */
#define LED_COLS   16                       /* COM16..COM31                  */
#define LED_ROWS   5
#define LED_REC    32                       /* bytes per common */
#define LED_FB     (LED_COLS * LED_REC)

/* Two complete frames, ping-pong. */
static __xdata UINT8 led_buf[2][LED_FB];
static __xdata UINT16 led_dma;                      /* DMA base of led_buf[led_show] */
static volatile UINT8 led_show;             /* frame the engine is streaming */
static volatile UINT8 led_work;             /* frame being drawn           */
static volatile __bit frame_ready;          /* led_work holds a whole frame */

static volatile __xdata UINT16 led_t;               /* animation clock, +led_speed per frame */
static __xdata UINT16 render_t;                     /* led_t latched for the frame being drawn */
static __xdata UINT8 render_chan;                   /* channel lit by the wiring-check effect */
static __xdata UINT16 led_boot_ms;
static __xdata UINT16 led_boot_len;               /* duration of this animation */
static __bit led_boot;
/* Rear-light state, worked out once per frame: every spare slot of the frame
 * then draws the same values, so a pattern cannot tear across commons. */
static __xdata UINT8 rear_i, rear_r, rear_g, rear_b;
static __xdata UINT8 led_back_fade;               /* reactive rear flash */
static __xdata UINT8 led_div;
static __bit led_is_on;

/* Frame being drawn.  Holding it in one pointer matters: indexing
 * led_buf[led_work] with a run-time row makes SDCC emit __mulint for every
 * byte, four per poke() and roughly 330 per frame -- more than the whole
 * ~11 ms frame period, so the renderer fell behind and any hiccup made the
 * panel repeat a frame (the visible "laggy" stutter).  One multiply per frame
 * instead. */
static __xdata UINT8 *frame;

/* Renderer timing, in 2 MHz timer ticks (0.5 us).  render_ms is the draw time
 * of the last frame rounded to ~1 ms, so the host can see how much of the
 * ~11 ms frame budget the renderer actually uses. */
static __xdata UINT16 render_t0;
static __xdata UINT16 render_dt;
static __xdata UINT8 render_ms;
static __xdata UINT8 phase0;
static __xdata UINT8 phase1;

UINT8 led_effect = LED_FX_RAINBOW;
UINT8 led_brightness = 80;      /* 0..127 master level (engine PWM is 6 bit) */
UINT8 led_speed = 4;            /* animation rate: 1 (slow) .. 8 (fast) */
UINT8 led_back = 1;             /* rear / underglow LEDs */
UINT8 led_back_level = 127;     /* rear / underglow brightness, 0..127 */
UINT8 led_back_rgb[3] = { 255, 255, 255 };
UINT8 led_boot_mode = LED_BOOT_SWEEP;
UINT8 led_boot_pace = 1;
UINT8 led_back_mode = LED_BACK_SOLID;
UINT8 led_cal[3] = { 128, 128, 128 };   /* white balance, 128 = unity */
UINT8 led_color[3] = { 255, 140, 60 };
volatile UINT8 led_irqs = 0;
volatile __xdata UINT16 led_frames = 0;   /* frames the engine has published */

/* Per-key picture for LED_FX_CUSTOM, filled by the host over the raw-HID
 * protocol and persisted in the data flash by settings.c. */
__xdata UINT8 led_custom[63][3];

/* Fast copy of led_cal so poke() does not touch the global three times. */


#ifdef OPENRGB
#include "openrgb.h"
#endif

/* Reactive effect: flash level per key. */
static __xdata UINT8 led_fade[LED_COLS * LED_ROWS];

/* ---- tiny helpers ---------------------------------------------------- */

/* Both helpers are used a few hundred times per frame, so they are macros:
 * as functions they cost a call and a return each, which was a large slice of
 * the ~11 ms budget on its own. */
#define scale8(v, s)        ((UINT8)(((UINT16)(v) * (s)) >> 8))

/* 0..255 triangle over 512 steps: smooth, no division, exactly periodic. */
#define tri16(x)            ((UINT8)(((x) & 0x100) ? \
                             (255 - ((x) & 0xFF)) : ((x) & 0xFF)))

/* hue in quarter units (0..1023) -> rgb, i.e. hue_q2/4 is the old 0..255 hue.
 *
 * Quarter steps exist so that animation speeds 1-3 do not judder.  The clock
 * advances by led_speed per published frame and the effects used to divide it
 * by 4, so at speed 1 the hue only changed every fourth frame (12.5 Hz) and
 * each step moved a colour channel by ~25 counts.  Taking the hue in quarter
 * units means speed 1 moves a quarter hue per frame instead, while speed 4
 * (t always a multiple of 4) produces exactly the old values.
 *
 * Division-free so it is cheap enough to run inside the LED interrupt. */
/* Results of hue2rgb() and pixel().  They are module globals rather than
 * output pointers because SDCC can only pass a pointer to a local as a
 * 3-byte generic pointer, and every store through one costs a __gptrput
 * call -- seven of them per LED, roughly 4 ms per frame.  Only the renderer
 * touches these, and it always runs from the main loop. */
static __xdata UINT8 hue_r, hue_g, hue_b;
static __xdata UINT8 pix_i, pix_r, pix_g, pix_b;

static void hue2rgb(UINT16 hue_q2)
{
    /* Sector and position both come out of the same product.  The old code
     * took the position as `hue - s * 43`, but a sector is 256/6 = 42.67 wide,
     * so at hues 128, 171 and 214 that subtraction underflows and wraps to
     * 255.  up/dn then came out inverted and a column flashed to the opposite
     * side of the wheel (6,2,252 instead of 6,252,252) for exactly one frame.
     * Rainbow sweeps those three hues about three times a second, and because
     * the colour is per column the whole column snapped -- the "not smooth"
     * everyone kept seeing.  Taking the low byte of hue*6 is the same maths
     * without the wrap.
     *
     * x = hue_q2 * 6 / 4 = hue_q2 + hue_q2 / 2, done without a multiply. */
    UINT16 x = (UINT16)(hue_q2 + (hue_q2 >> 1));  /* 0..1534              */
    UINT8 s = (UINT8)(x >> 8);              /* sector 0..5             */
    UINT8 f = (UINT8)(x & 0xFF);            /* position in sector 0..255 */
    UINT8 up = (UINT8)(((UINT16)f * 252) >> 8);
    UINT8 dn = (UINT8)(252 - up);

    switch (s) {
    case 0:  hue_r = 252; hue_g = up;  hue_b = 6;   break;
    case 1:  hue_r = dn;  hue_g = 252; hue_b = 6;   break;
    case 2:  hue_r = 6;   hue_g = 252; hue_b = up;  break;
    case 3:  hue_r = 6;   hue_g = dn;  hue_b = 252; break;
    case 4:  hue_r = up;  hue_g = 6;   hue_b = 252; break;
    default: hue_r = 252; hue_g = 6;   hue_b = dn;  break;
    }
}

/* ---- hardware side --------------------------------------------------- */

/* Stream the current common's 32 bytes into the PWM shadow registers.
 * Sets E_DIS so nothing can preempt the read burst; SETB P copies LED_DMA
 * into DPTR (CH555 specific). */
static void led_pump(void)
{
    __asm
        setb 0xAE               /* E_DIS  */
        setb 0xD0               /* P -> copy LED_DMA into DPTR */
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        movx a,@dptr
        clr 0xAE
    __endasm;
}

static void led_common_low(UINT8 com)
{
    UINT8 n = (UINT8)(com - 0x10);

    if (n < 8) {
        P0 = (UINT8)(P0 & (UINT8)~(1 << n));
    } else {
        P3 = (UINT8)(P3 & (UINT8)~(1 << (n - 8)));
    }
}

/* ---- frame writing --------------------------------------------------- */

static void poke(UINT16 off, UINT8 inten, UINT8 r, UINT8 g, UINT8 b)
{
    /* The engine runs in 6-bit brightness mode (64-clock intensity PWM), so
     * the record's intensity byte only has 6 meaningful bits.  Everything
     * above this line still works in the old 0..127 range and is scaled here,
     * once, instead of at each of the ~500 pokes per frame. */
    frame[off]     = (UINT8)(inten >> 1);

    /* White balance.  Three branches, taken only when a channel has actually
     * been moved off unity, so the default path costs nothing. */
    if (led_cal[0] != 128) {
        UINT16 t = ((UINT16)r * led_cal[0]) >> 7;
        r = t > 255 ? 255 : (UINT8)t;
    }
    if (led_cal[1] != 128) {
        UINT16 t = ((UINT16)g * led_cal[1]) >> 7;
        g = t > 255 ? 255 : (UINT8)t;
    }
    if (led_cal[2] != 128) {
        UINT16 t = ((UINT16)b * led_cal[2]) >> 7;
        b = t > 255 ? 255 : (UINT8)t;
    }

    frame[off + 1] = r;
    frame[off + 2] = g;
    frame[off + 3] = b;
}

/* Per-channel brightness trim, 128 = unity.  The five key rows land on
 * channels 1..5 and the rear strip on channel 0.  Panels are not perfectly
 * even, so each channel can be trimmed from the keyboard in the channel-walk
 * effect and the result is stored in flash. */
__xdata UINT8 led_gain[8] = { 128, 128, 128, 128, 128, 128, 128, 128 };


static UINT8 trim(UINT8 v, UINT8 gain)
{
    UINT16 t = ((UINT16)v * gain) >> 7;

    return t > 127 ? 127 : (UINT8)t;
}

/* Channel shown by the wiring-check effect.  Latched with the frame clock so
 * the whole frame shows the same channel, and so the gain keys always trim the
 * channel the user is looking at.  Advances every couple of seconds. */
static UINT8 chan_sel(void)
{
    return (UINT8)((render_t >> 9) & 7);
}

/* All eight channels of one common dark. */
static void render_blank_col(UINT8 col)
{
    UINT8 ch;
    UINT16 base = (UINT16)((UINT16)col << 5);

    for (ch = 0; ch < 8; ch++) {
        poke((UINT16)(base + ((UINT16)ch << 2)), 0, 0, 0, 0);
    }
}

/* Cheap 8-bit hash used by the sparkle/random effects.  Not a PRNG: it only
 * has to look unrelated between neighbouring keys and successive frames. */
static UINT8 mix8(UINT16 a)
{
    UINT8 x = (UINT8)a;

    x ^= (UINT8)(x << 3);
    x = (UINT8)(x + (UINT8)(a >> 8) + 0x37);
    x ^= (UINT8)(x >> 5);
    return (UINT8)(x * 29);
}

/* Distance from the centre of the 14 x 5 grid, in grid steps. */
static UINT8 centre_dist(UINT8 col, UINT8 row)
{
    UINT8 c = (col > 6) ? (UINT8)(col - 6) : (UINT8)(6 - col);
    UINT8 r = (row > 2) ? (UINT8)(row - 2) : (UINT8)(2 - row);

    return (UINT8)(c + r);
}

/* Colour of one LED for the active effect.  t is the latched frame clock so
 * every LED of a frame is drawn at the same animation step. */
static void pixel(UINT8 col, UINT8 row, UINT16 t)
{
    UINT8 r = 0, g = 0, b = 0;
    UINT8 inten = led_brightness;
    UINT8 key = 0;
    UINT8 v = 0, k = 0, h = 0, d = 0, p = 0, c2 = 0, r2 = 0;

    switch (led_effect) {
    case LED_FX_OFF:
        inten = 0;
        break;

    case LED_FX_SOLID:
        r = led_color[0]; g = led_color[1]; b = led_color[2];
        break;

    case LED_FX_BREATH:
        v = tri16((UINT16)(t >> 1));
        r = scale8(led_color[0], v);
        g = scale8(led_color[1], v);
        b = scale8(led_color[2], v);
        break;

    case LED_FX_RAINBOW:
        /* vertical bands of hue marching across the board, in quarter-hue
         * steps so slow speeds still move smoothly.  A column is 18 hues
         * apart = 72 quarter units; the clock is masked into the same
         * 1024-step period, and 65536 is a multiple of 1024, so the 16-bit
         * clock can wrap without a seam. */
        hue2rgb((UINT16)((col * 72 + t) & 1023));
        r = hue_r; g = hue_g; b = hue_b;
        break;

    case LED_FX_AURORA:
        /* teal at the top to violet at the bottom, with a slow shimmer
         * rolling diagonally through it */
        k = (UINT8)(96 + (tri16((UINT16)((t >> 1) + col * 12 + row * 20)) >> 1));
        hue2rgb((UINT16)((128 + row * 22) << 2));
        r = scale8(hue_r, k); g = scale8(hue_g, k); b = scale8(hue_b, k);
        break;

    case LED_FX_WAVE:
        /* A bright cyan crest sweeping diagonally over a dim base.  The
         * spatial offset is a multiple of 4 quarter-units and the arithmetic
         * is 16 bit, so a temporal wrap of 1024 maps the pattern exactly onto
         * itself: the crest slides for ever without ever jumping back.
         *
         * The crest does not pop: the ramp fades back up to the crest level
         * over the last quarter of the cycle, so the wrap is continuous. */
        d = (UINT8)(((UINT16)((col << 6) + (row << 5)) - t) >> 2);
        if (d < 96) {
            v = (UINT8)(255 - ((UINT16)d << 1));           /* crest 255..63 */
        } else if (d < 128) {
            v = (UINT8)(63 - (UINT8)(((UINT16)(d - 96) * 23) >> 5)); /* 63..41 */
        } else {
            v = (UINT8)(40 + (UINT8)(((UINT16)(d - 128) * 215) >> 7)); /* 40..253 */
        }
        r = scale8(0, v); g = scale8(200, v); b = scale8(255, v);
        break;

    case LED_FX_REACTIVE:
        v = led_fade[(UINT8)(col * LED_ROWS + row)];
        if (v) {
            r = 255; g = 235; b = 210;
        } else {
            r = 0; g = 40; b = 110;
            inten = (UINT8)(led_brightness >> 2);
        }
        break;

    case LED_FX_SPECTRUM:
        hue2rgb((UINT16)(((UINT16)row * 72 + (t << 1)) & 1023));
        r = hue_r; g = hue_g; b = hue_b;
        break;

    case LED_FX_RAINWAVE:
        hue2rgb((UINT16)((col * 40 + row * 56 + t) & 1023));
        r = hue_r; g = hue_g; b = hue_b;
        break;

    case LED_FX_COLORWAVE:
        v = tri16((UINT16)(col * 24 + row * 16 - t));
        r = scale8(led_color[0], v);
        g = scale8(led_color[1], v);
        b = scale8(led_color[2], v);
        break;

    case LED_FX_GRADIENT:
        /* top row full, bottom row dim, with a slow shimmer */
        k = (UINT8)(200 - row * 34 + (tri16((UINT16)(t >> 2)) >> 4));
        r = scale8(led_color[0], k);
        g = scale8(led_color[1], k);
        b = scale8(led_color[2], k);
        break;

    case LED_FX_SPIRAL:
        v = (UINT8)(120 + (tri16((UINT16)(t + col * 16)) >> 1));
        hue2rgb((UINT16)((col * 48 + row * 96 + (t << 1)) & 1023));
        r = scale8(hue_r, v); g = scale8(hue_g, v); b = scale8(hue_b, v);
        break;

    case LED_FX_CHECKER:
        if (((col + row) & 1) ^ ((t >> 7) & 1)) {
            r = led_color[0]; g = led_color[1]; b = led_color[2];
        } else {
            r = scale8(led_color[0], 40);
            g = scale8(led_color[1], 40);
            b = scale8(led_color[2], 40);
        }
        break;

    case LED_FX_STROBE:
        if ((t >> 6) & 1) {
            r = led_color[0]; g = led_color[1]; b = led_color[2];
        } else {
            inten = 0;
        }
        break;

    case LED_FX_ALTERNATE:
        if ((row & 1) ^ ((t >> 7) & 1)) {
            r = led_color[0]; g = led_color[1]; b = led_color[2];
        } else {
            r = scale8(led_color[0], 32);
            g = scale8(led_color[1], 32);
            b = scale8(led_color[2], 32);
        }
        break;

    case LED_FX_TWINKLE:
        h = mix8((UINT16)((t >> 2) + col * 17 + row * 29));
        if (h < 24) {
            r = 255; g = 255; b = 255;
        } else {
            r = scale8(led_color[0], 26);
            g = scale8(led_color[1], 26);
            b = scale8(led_color[2], 26);
        }
        break;

    case LED_FX_STARLIGHT:
        h = mix8((UINT16)((t >> 3) + col * 37 + row * 61));
        if (h < 10) {
            r = 220; g = 230; b = 255;
        } else if (h < 16) {
            r = 90; g = 100; b = 130;
        } else {
            inten = 0;
        }
        break;

    case LED_FX_FIRE:
        h = mix8((UINT16)((t >> 1) + col * 11 + row * 23));
        v = (UINT8)(h >> 1);
        k = (UINT8)(80 + row * 30);
        r = 255;
        g = scale8(v, k);
        b = (UINT8)(v >> 4);
        break;

    case LED_FX_OCEAN:
        v = tri16((UINT16)((t >> 1) + col * 20 + row * 34));
        r = 0;
        g = scale8(90, v);
        b = scale8(255, v);
        break;

    case LED_FX_SUNSET:
        hue2rgb((UINT16)((4 + row * 26) << 2));
        r = hue_r; g = hue_g; b = hue_b;
        break;

    case LED_FX_VAPOR:
        hue2rgb((UINT16)((col * 24 + row * 18 + (t >> 2)) & 1023));
        /* half-way to white: soft pastel */
        r = (UINT8)((hue_r >> 1) + 128);
        g = (UINT8)((hue_g >> 1) + 128);
        b = (UINT8)((hue_b >> 1) + 128);
        break;

    case LED_FX_MATRIX:
        h = mix8((UINT16)((t >> 2) + col * 53));
        v = tri16((UINT16)(h + row * 40));
        r = 0;
        g = scale8(255, v);
        b = scale8(40, v);
        break;

    case LED_FX_SNOW:
        r2 = (UINT8)((row + (t >> 6) + col) % LED_ROWS);
        h = mix8((UINT16)((t >> 4) + col * 41 + r2 * 19));
        if (h < 34) {
            r = 235; g = 240; b = 255;
        } else {
            r = 0; g = 8; b = 20;
        }
        break;

    case LED_FX_RIPPLE:
        v = tri16((UINT16)(centre_dist(col, row) * 70) - t);
        r = scale8(0, v); g = scale8(170, v); b = scale8(255, v);
        break;

    case LED_FX_BREATHRAIN:
        v = tri16((UINT16)(t >> 1));
        hue2rgb((UINT16)((col * 72 + t) & 1023));
        r = scale8(hue_r, v); g = scale8(hue_g, v); b = scale8(hue_b, v);
        break;

    case LED_FX_HEARTBEAT:
        p = (UINT8)((t >> 3) & 0x7F);
        v = 0;
        if (p < 16) {
            v = (UINT8)(p * 16);
        } else if (p < 30) {
            v = (UINT8)(255 - (p - 16) * 12);
        } else if (p < 44) {
            v = (UINT8)((p - 29) * 16);
        } else if (p < 60) {
            v = (UINT8)(239 - (p - 44) * 14);
        }
        r = scale8(255, v); g = scale8(20, v); b = scale8(30, v);
        break;

    case LED_FX_SCANNER:
        p = (UINT8)((t >> 3) % 28);
        c2 = (UINT8)(col << 1);
        d = (p > c2) ? (UINT8)(p - c2) : (UINT8)(c2 - p);
        v = (d > 8) ? 20 : (UINT8)(235 - d * 25);
        r = scale8(led_color[0], v);
        g = scale8(led_color[1], v);
        b = scale8(led_color[2], v);
        break;

    case LED_FX_LASER:
        p = (UINT8)((t >> 3) % 10);
        r2 = (UINT8)(row << 1);
        d = (p > r2) ? (UINT8)(p - r2) : (UINT8)(r2 - p);
        v = (d > 4) ? 16 : (UINT8)(240 - d * 50);
        r = scale8(led_color[0], v);
        g = scale8(led_color[1], v);
        b = scale8(led_color[2], v);
        break;

    case LED_FX_PULSE:
        d = centre_dist(col, row);
        p = (UINT8)((t >> 4) % 8);
        v = (d == p) ? 255 : ((d + 1 == p) ? 90 : 8);
        r = scale8(0, v); g = scale8(210, v); b = scale8(255, v);
        break;

    case LED_FX_CYCLEROW:
        if (((t >> 5) % LED_ROWS) == row) {
            r = led_color[0]; g = led_color[1]; b = led_color[2];
        } else {
            inten = 0;
        }
        break;

    case LED_FX_RANDOMFADE:
        h = mix8((UINT16)((t >> 3) + col * 7 + row * 13));
        v = tri16((UINT16)((UINT16)h * 3 + (t >> 1)));
        r = scale8(led_color[0], v);
        g = scale8(led_color[1], v);
        b = scale8(led_color[2], v);
        break;

    case LED_FX_HEATMAP:
        v = led_fade[(UINT8)(col * LED_ROWS + row)];
        if (v) {
            r = 255;
            g = scale8(v, 190);
            b = 0;
        } else {
            r = 0; g = 10; b = 28;
            inten = (UINT8)(led_brightness >> 1);
        }
        break;

    case LED_FX_RAINREACT:
        v = led_fade[(UINT8)(col * LED_ROWS + row)];
        if (v) {
            hue2rgb((UINT16)((col * 72 + row * 50 + t) & 1023));
            r = hue_r; g = hue_g; b = hue_b;
        } else {
            r = 0; g = 34; b = 90;
            inten = (UINT8)(led_brightness >> 2);
        }
        break;

    case LED_FX_CANDY:
        hue2rgb((UINT16)((((UINT16)col * 2 + row * 3) * 128 + (t >> 3)) & 1023));
        if ((col + row) & 1) {
            r = hue_r; g = hue_g; b = hue_b;
        } else {
            r = scale8(hue_r, 130);
            g = scale8(hue_g, 130);
            b = scale8(hue_b, 130);
        }
        break;

    case LED_FX_CUSTOM:
        key = keymap_rowkey(col, row);
        if (key == 0xFF) {
            inten = 0;
        } else {
            r = led_custom[key][0];
            g = led_custom[key][1];
            b = led_custom[key][2];
        }
        break;

    case LED_FX_TEST:
    default:
        switch (row) {
        case 0: r = 255; g = 0;   b = 0;   break;  /* red    */
        case 1: r = 0;   g = 255; b = 0;   break;  /* green  */
        case 2: r = 0;   g = 0;   b = 255; break;  /* blue   */
        case 3: r = 255; g = 255; b = 255; break;  /* white  */
        default:r = 255; g = 120; b = 0;   break;  /* orange */
        }
        break;
    }

    pix_i = inten;
    pix_r = r; pix_g = g; pix_b = b;
}

/* Is (col, ch) an LED position that no key uses?  Keys occupy channels 1..5;
 * channel 0 and 6..7 are free, and so is every channel of a common that has
 * no key.  The rear / underglow LEDs sit somewhere in that free set, so all
 * of it is driven: that lights them without having to know the mapping. */
static __bit spare_slot(UINT8 col, UINT8 ch)
{
    if (col >= MATRIX_COLS) {
        return 1;                   /* back-light commons have no keys */
    }
    if (ch == 0 || ch >= 6) {
        /* These used to be spare.  They are not on this board: the top key
         * row is wired to one of them, so row 0 owns all three (see the
         * mirror in render_col) and the rear colour must not overwrite it. */
        return 0;
    }
    return (keymap_rowkey(col, (UINT8)(ch - 1)) == 0xFF) ? 1 : 0;
}

/* The back light is driven from every spare slot, but only COM30/31 are wired
 * to the six rear LEDs.  Their physical order is unknown, so patterns animate
 * in time and light all six together (see rear_state). */

/* Rear-light values for the frame being drawn.  Called once per frame with the
 * latched clock so every spare slot of a frame shows the same instant. */
static void rear_state(UINT16 t) __reentrant
{
    UINT8 inten = led_back_level;

    rear_i = 0;
    if (!led_back || !led_is_on) {
        return;
    }

#ifdef OPENRGB
    /* Direct mode is a picture, not an animation, and the underglow is part of
     * that picture.  Without this branch the key backlights followed the host
     * while the strip kept running whatever rear animation was selected, so a
     * Direct paint looked dead on the six rear LEDs.  They are physically
     * driven from one state, so the host's six colours are averaged into the
     * single colour the strip can show.  The boot animation keeps its own rear
     * light, which is why led_boot is excluded. */
    if (openrgb_direct_active && !led_boot) {
        UINT16 r = 0, g = 0, b = 0;
        UINT8 i;

        for (i = 0; i < OPENRGB_UNDERGLOW_COUNT; i++) {
            r += openrgb_colors[OPENRGB_KEY_COUNT + i][0];
            g += openrgb_colors[OPENRGB_KEY_COUNT + i][1];
            b += openrgb_colors[OPENRGB_KEY_COUNT + i][2];
        }
        rear_r = (UINT8)(r / OPENRGB_UNDERGLOW_COUNT);
        rear_g = (UINT8)(g / OPENRGB_UNDERGLOW_COUNT);
        rear_b = (UINT8)(b / OPENRGB_UNDERGLOW_COUNT);
        /* The strip keeps its own level control.  Using led_brightness here
         * would make the Lighting page's rear Level slider dead in Direct,
         * and the key dimmer would silently move the strip too. */
        rear_i = led_back_level;
        return;
    }
#endif

    switch (led_back_mode) {
    case LED_BACK_BREATH: {
        UINT8 v = tri16((UINT16)(t >> 1));

        inten = (UINT8)(((UINT16)inten * v) >> 8);
        break;
    }
    case LED_BACK_HEARTBEAT: {
        UINT8 p = (UINT8)t;
        UINT8 v = 0;

        if (p < 16) {
            v = (UINT8)(255 - p * 10);
        } else if (p >= 28 && p < 44) {
            v = (UINT8)(190 - (UINT8)(p - 28) * 10);
        }
        inten = (UINT8)(((UINT16)inten * v) >> 8);
        break;
    }
    case LED_BACK_PULSE: {
        UINT8 v = tri16((UINT16)(t >> 2));

        v = scale8(v, v);               /* sharper than a sine */
        inten = (UINT8)(((UINT16)inten * v) >> 8);
        break;
    }
    case LED_BACK_RAINBOW:
        hue2rgb((UINT16)(t << 1));
        rear_r = hue_r;
        rear_g = hue_g;
        rear_b = hue_b;
        rear_i = inten;
        return;
    case LED_BACK_REACTIVE: {
        /* Idle at a third, flare to full on a key, fall back in ~130 ms. */
        UINT8 f = led_back_fade;

        inten = (UINT8)(((UINT16)inten *
                         (UINT16)(85 + scale8(f, 170))) >> 8);
        break;
    }
    case LED_BACK_STROBE:
        /* Hard gate: five frames lit, three dark (~6.7 Hz at 89 fps). */
        if ((t & 7) < 3) {
            inten = 0;
        }
        break;
    case LED_BACK_SPARKLE: {
        /* Deterministic hash of the frame time, so two commons drawing the
         * same instant always agree. */
        UINT8 h = mix8((UINT16)(t * 7));

        if (h > 208) {
            inten = 255;                /* brief flare */
        } else if (h > 150) {
            inten = (UINT8)(inten >> 1);
        } else {
            inten = (UINT8)(inten >> 3);
        }
        break;
    }
    case LED_BACK_WAVE:
        /* Hue drifts while a swell rolls over it. */
        hue2rgb((UINT16)(t * 3));
        rear_r = hue_r;
        rear_g = hue_g;
        rear_b = hue_b;
        rear_i = (UINT8)(((UINT16)inten *
                          (UINT16)(120 + (tri16((UINT16)(t >> 1)) >> 1))) >> 8);
        return;
    default:
        break;
    }

    rear_r = led_back_rgb[0];
    rear_g = led_back_rgb[1];
    rear_b = led_back_rgb[2];
    rear_i = inten;
}

/* ---- start-up animation ---------------------------------------------- */

/* Duration of the selected animation, scaled by the pace.  Fast is 70%,
 * normal 100%, slow 145%. */
static UINT16 boot_length(void) __reentrant
{
    UINT16 base;

    switch (led_boot_mode) {
    case LED_BOOT_SWEEP:   base = 1300; break;
    case LED_BOOT_RAINBOW: base = 1150; break;
    case LED_BOOT_FADE:    base = 900;  break;
    case LED_BOOT_SCAN:    base = 1150; break;
    case LED_BOOT_PULSE:   base = 1320; break;
    default:               return 0;
    }
    if (led_boot_pace == 0) {
        base = (UINT16)((base * 70) / 100);
    } else if (led_boot_pace >= 2) {
        base = (UINT16)((base * 145) / 100);
    }
    return base;
}

static void boot_start(void)
{
    led_boot_len = boot_length();
    if (!led_boot_len) {
        led_boot = 0;
        led_boot_ms = 0;
        return;
    }
    led_boot = 1;
    led_boot_ms = 0;
}

void led_boot_preview(void)
{
    boot_start();
}

/* One column of the selected start-up animation. */
static void render_boot_col(UINT8 col) __reentrant
{
    UINT8 row, ch;
    UINT16 ms = led_boot_ms;

    if (!led_is_on) {
        render_blank_col(col);
        return;
    }

    for (row = 0; row < LED_ROWS && col < MATRIX_COLS; row++) {
        UINT16 idx, d;
        UINT8 inten = 0, r = 0, g = 0, b = 0;

        if (keymap_rowkey(col, row) == 0xFF) {
            continue;
        }
        idx = (UINT16)(((UINT16)col << 5) + ((UINT16)(row + 1) << 2));

        switch (led_boot_mode) {
        case LED_BOOT_SWEEP:
            d = (UINT16)(col * 45 + row * 18);
            if (ms > d) {
                UINT16 e = (UINT16)(ms - d);

                if (e < 510) {
                    UINT8 v = (UINT8)(255 - (e >> 1));

                    inten = led_brightness;
                    r = v;
                    g = (UINT8)(((UINT16)v * 3) >> 2);
                    b = (UINT8)(v >> 1);
                }
            }
            break;

        case LED_BOOT_RAINBOW:
            d = (UINT16)(col * 40 + row * 15);
            if (ms > d) {
                UINT16 e = (UINT16)(ms - d);
                UINT8 v = (e < 220) ? (UINT8)(e + 35) : 255;

                hue2rgb((UINT16)(col * 64 + row * 22));
                inten = led_brightness;
                r = scale8(hue_r, v);
                g = scale8(hue_g, v);
                b = scale8(hue_b, v);
            }
            break;

        case LED_BOOT_FADE: {
            UINT8 v = (ms >= 700) ? 255 : (UINT8)((ms * 255) / 700);

            inten = scale8(led_brightness, v);
            r = 255;
            g = 236;
            b = 214;
            break;
        }

        case LED_BOOT_SCAN: {
            UINT8 pos = (UINT8)(ms / 55);   /* bar has left the board */

            if (pos >= LED_COLS) {
                inten = led_brightness;
                r = 255; g = 246; b = 230;
            } else if (pos == 0) {
                /* nothing lit yet */
            } else if ((UINT8)(col + 1) < pos) {
                inten = (UINT8)(led_brightness >> 1);
                r = 255; g = 205; b = 150;
            } else if ((UINT8)(col + 1) == pos) {
                inten = led_brightness;
                r = 255; g = 246; b = 230;
            }
            break;
        }

        case LED_BOOT_PULSE: {
            UINT16 p = (UINT16)(ms % 400);
            UINT8 v;

            v = (p < 200) ? (UINT8)((p * 255) / 200)
                          : (UINT8)((255 - ((p - 200) * 255) / 200));
            inten = scale8(led_brightness, v);
            r = 255;
            g = 220;
            b = 180;
            break;
        }

        default:
            break;
        }

        poke(idx, inten, r, g, b);

        /* Same row-0 mirror as render_col: the top row lives on channels
         * 0/6/7, so without this it stayed dark through every start-up
         * animation. */
        if (row == 0) {
            UINT16 b0 = (UINT16)((UINT16)col << 5);

            poke((UINT16)(b0 + ((UINT16)0 << 2)), inten, r, g, b);
            poke((UINT16)(b0 + ((UINT16)6 << 2)), inten, r, g, b);
            poke((UINT16)(b0 + ((UINT16)7 << 2)), inten, r, g, b);
        }
    }

    /* The rear light runs its own animation during the start-up as well, so a
     * dim effect never leaves the strip looking dead at power-on. */
    for (ch = 0; ch < 8; ch++) {
        UINT16 idx;

        if (!spare_slot(col, ch)) {
            continue;
        }
        idx = (UINT16)(((UINT16)col << 5) + ((UINT16)ch << 2));
        poke(idx, rear_i, rear_r, rear_g, rear_b);
    }
}

/* One column of the active effect, drawn into led_buf[led_work]. */
static void render_col(UINT8 col)
{
    UINT8 row, ch;
    UINT8 inten = 0, r = 0, g = 0, b = 0;
    UINT8 shared;
    UINT16 base = (UINT16)((UINT16)col << 5);

    if (!led_is_on) {
        render_blank_col(col);
        return;
    }

    /* Wiring check: light exactly one channel, full white.  Fn + up/down
     * trims that channel's gain while it is lit. */
    if (led_effect == LED_FX_CHAN) {
        for (ch = 0; ch < 8; ch++) {
            UINT8 on = (UINT8)(ch == render_chan);

            poke((UINT16)(base + ((UINT16)ch << 2)),
                 on ? trim(led_brightness, led_gain[ch]) : 0,
                 on ? 255 : 0,
                 on ? 255 : 0,
                 on ? 255 : 0);
        }
        return;
    }

    /* Solid, breathing, rainbow and off give every row the same colour, so
     * the hue is computed once per column instead of once per LED.  Rainbow
     * is the default effect, and this is five sixths of its per-frame maths. */
    shared = (led_effect == LED_FX_SOLID || led_effect == LED_FX_BREATH ||
              led_effect == LED_FX_RAINBOW || led_effect == LED_FX_OFF);
    if (shared) {
        pixel(col, 0, render_t);
        inten = pix_i; r = pix_r; g = pix_g; b = pix_b;
    }

    for (row = 0; row < LED_ROWS && col < MATRIX_COLS; row++) {
        UINT8 key = keymap_rowkey(col, row);
        UINT16 off;

        if (key == 0xFF) {
            continue;
        }
        off = (UINT16)(base + ((UINT16)(row + 1) << 2));

#ifdef OPENRGB
        if (openrgb_direct_active) {
            /* The intensity byte is the dimmer.  Scaling the colour by the
             * brightness *as well* squared the attenuation and made direct
             * mode look about five times darker than the onboard effects. */
            inten = trim(led_brightness, led_gain[row + 1]);
            r = openrgb_colors[key][0];
            g = openrgb_colors[key][1];
            b = openrgb_colors[key][2];
        } else
#endif
        {
            if (!shared) {
                pixel(col, row, render_t);
                inten = pix_i; r = pix_r; g = pix_g; b = pix_b;
            }
            if (inten) {
                inten = trim(inten, led_gain[row + 1]);
            }
        }

        poke(off, inten, r, g, b);

        /* The top key row is not on channel 1 with the other four: its LEDs
         * sit on the channels the engine otherwise leaves spare (0, 6 and 7).
         * Until this mirror existed the row was painted with the rear-light
         * colour -- dark whenever the rear light was off -- and never followed
         * the key effect.  Writing the row-0 colour to all three slots is safe
         * because only the wired one is visible, and the 14 key commons are
         * the only ones touched, so COM30/31 keep their six rear LEDs. */
        if (row == 0) {
            poke((UINT16)(base + ((UINT16)0 << 2)), inten, r, g, b);
            poke((UINT16)(base + ((UINT16)6 << 2)), inten, r, g, b);
            poke((UINT16)(base + ((UINT16)7 << 2)), inten, r, g, b);
        }
    }

    /* Back light: led6..led11 on the back panel plus the side strip.  These
     * LEDs are fed from the spare slots; rear_state() has already worked out
     * this frame's level and colour from the selected rear animation. */
    for (ch = 0; ch < 8; ch++) {
        UINT16 off = (UINT16)(base + ((UINT16)ch << 2));

        if (!spare_slot(col, ch)) {
            continue;
        }
        poke(off, rear_i, rear_r, rear_g, rear_b);
    }
}

/* ---- public ---------------------------------------------------------- */

void led_init(void)
{
    UINT16 i;

    for (i = 0; i < (UINT16)(sizeof(led_buf)); i++) {
        ((__xdata UINT8 *)led_buf)[i] = 0;
    }
    for (i = 0; i < sizeof(led_fade); i++) {
        led_fade[i] = 0;
    }
    for (i = 0; i < (UINT16)sizeof(led_custom); i++) {
        ((__xdata UINT8 *)led_custom)[i] = 0;
    }
    led_show = 0;
    led_work = 1;
    frame_ready = 0;
    led_dma = (UINT16)&led_buf[0][0];
    led_t = 0;
    led_div = 0;
    led_is_on = 1;
    led_irqs = 0;

    /* Timer 1 free-runs at Fsys/12 = 2 MHz.  Only the renderer reads it, to
     * measure how much of the ~11 ms frame budget a frame actually uses. */
    TMOD = (TMOD & 0x0F) | 0x10;
    TR1 = 1;
    frame = &led_buf[0][0];

    /* P4/P2/P1 sinks (RED/GREEN/BLUE PWM), P0/P3 commons. */
    P4 = 0xFF;
    P2 = 0xFF;
    P1 = 0xFF;
    P0 = 0xFF;
    P3 = 0xFF;
    P4_MOD_OC = 0x00;
    P2_MOD_OC = 0x00;
    P1_MOD_OC = 0x00;
    P0_MOD_OC = 0x00;
    P3_MOD_OC = 0x00;
    P4_DIR_PU = 0xFF;       /* pull-ups for the current-mode key inputs */
    P0_DIR_PU = 0xFF;
    P3_DIR_PU = 0xFF;
    P1_DIR_PU = 0xFF;
    P2_DIR_PU = 0xFF;

    LED_CTRL = bLED_EN;
    /* 8-bit colour PWM (256 clocks) with 6-bit brightness PWM (64 clocks).
     * The intensity PWM cycle also sets the per-common slot time and the
     * inhibition timer is counted in those cycles, so dropping 128 -> 64 took
     * the common slot from 1365 us to ~800 us.  With 16 commons (the 14 matrix
     * columns plus COM30/31 for the back light) that is ~11 ms per frame,
     * about 89 Hz instead of the 19 ms / 52 Hz of the 14-common 7-bit build.
     * poke() shifts the 0..127 intensity range down to the 6 bits the record
     * actually carries, so nothing above it had to change.
     *
     * bLED_COLOR_CYC must stay clear.  With it set the colour registers become
     * 7 bit wide, so every 8-bit colour value written by the effects and by
     * OpenRGB was truncated: components landed at roughly half their intended
     * duty, and values just under a power of two collapsed towards zero.  That
     * is what made some keys look dim and others almost black. */
    LED_CYCLE = bLED_INTEN_CYC1;
    /* 8 brightness cycles of inhibition time to stream the next record. */
    LED_FRAME = bLED_INH_TMR2 | bLED_INH_TMR0;
    LED_DMA_L = (UINT8)led_dma;
    LED_DMA_H = (UINT8)(led_dma >> 8);
    /* No constant brightness offset: LED_INT_ADJ is added to every loaded
     * brightness value, so a non-zero value stops "brightness 0" from being
     * truly off and lifts the black of every effect. */
    LED_INT_ADJ = 0x00;
    LED_PWM_OE = 0xFF;
    LED_COMMON = 0x1F;      /* first interrupt wraps to COM16 / P0.0 */
    LED_CTRL = bLED_IE_INHIB | bLED_BLUE_EN | bLED_GREEN_EN | bLED_RED_EN |
               bLED_COM_AHEAD | bLED_PWM_INHIB | bLED_EN;
    LED_STATUS |= bLED_IF_SET;   /* force the first inhibition interrupt */

    /* Give the LED engine the high interrupt level.  The inhibition window is
     * only ~22 us long and the panel dims the common that was serviced late,
     * so a USB or UART interrupt arriving at the wrong moment showed up as a
     * flickering or dim row.  The LED ISR is short, so letting it preempt USB
     * costs nothing. */
    IP_EX |= bIP_PWM_I2C;
    IE_PWM_I2C = 1;

    boot_start();               /* play the selected start-up animation */
}

void led_shutdown(void)
{
    IE_PWM_I2C = 0;
    LED_CTRL = 0;
    LED_PWM_OE = 0;
    LED_COMMON = 0;
    LED_STATUS = 0;

    P4_LED_KEY = 0x00;
    P0 = 0xFF;
    P3 = 0xFF;
    P4 = 0xFF;
    P1 = 0xFF;
    P2 = 0xFF;
    P0_MOD_OC = 0xFF; P0_DIR_PU = 0xFF;
    P3_MOD_OC = 0xFF; P3_DIR_PU = 0xFF;
    P4_MOD_OC = 0xFF; P4_DIR_PU = 0xFF;
    P1_MOD_OC = 0xFF; P1_DIR_PU = 0xFF;
    P2_MOD_OC = 0xFF; P2_DIR_PU = 0xFF;
}

/* Direct mode is selected but the host has not sent a single colour yet.
 * Publishing now would show the stale array, which is all zero after boot, so
 * the whole board went black until OpenRGB got round to its first SET_LEDS.
 * Holding the current frame is the fix, and this predicate exists so the
 * "interrupts arrive but no frame is published" fallback in main() can tell
 * that pause apart from a genuinely unusable inhibition scan. */
__bit led_paused(void)
{
#ifdef OPENRGB
    /* A boot preview draws its own frame and does not read openrgb_colors, so
     * the hold must not swallow it: asking for a preview while Direct was
     * selected used to do nothing at all. */
    if (led_boot) {
        return 0;
    }
    return (openrgb_direct_active && !openrgb_have_colors) ? 1 : 0;
#else
    return 0;
#endif
}

void led_irq(void)
{
    UINT8 com;
    UINT8 idx;
    UINT16 addr;

    /* Commons are COM16..COM31, i.e. LED_COMMON 0x10..0x1F.  The last two
     * (COM30/31 on P3.6/P3.7) carry the back light.  Wrapping at 0x1D silently
     * dropped the last column, and since the matrix frame is only considered
     * complete after that column, no key was ever reported. */
    com = (UINT8)(LED_COMMON + 1);
    if (com < 0x10 || com > 0x1F) {
        com = 0x10;

        /* Frame boundary.  Publish the buffer the main loop finished drawing;
         * if it is not ready yet, keep showing the current frame so the panel
         * never shows half an effect.  The animation clock advances only for a
         * frame that is actually published. */
        if (frame_ready) {
            led_show = led_work;
            led_work = (UINT8)(led_work ^ 1);
            /* Two link-time constants, not led_buf[led_show].  Indexing the
             * array at run time makes SDCC call __mulint, whose parameter
             * block lives in the shared overlay area at 0x73 -- the very
             * bytes raw_hid_send() keeps its source pointer in.  This ISR
             * runs while the main loop is copying an OpenRGB reply, so the
             * call truncated a reply roughly once per LED frame. */
            if (led_show) {
                led_dma = (UINT16)&led_buf[1][0];
            } else {
                led_dma = (UINT16)&led_buf[0][0];
            }
            led_frames++;
            led_t = (UINT16)(led_t + led_speed);
            frame_ready = 0;
        }

        keyboard_frame_end();
    }
    LED_COMMON = com;               /* also clears bLED_IF */

    idx = (UINT8)(com - 0x10);
    addr = (UINT16)(led_dma + ((UINT16)idx << 5));
    LED_DMA_L = (UINT8)addr;
    LED_DMA_H = (UINT8)(addr >> 8);

    led_pump();

    /* Only the first 14 commons are matrix columns.  COM30/31 are back-light
     * only: forcing those pins low would fight the engine's own common drive,
     * and there is no row to read there. */
    if (idx < MATRIX_COLS) {
        /* Hold this common low so its matrix column can be read, then hand P4
         * back to the key-scan current inputs. */
        led_common_low(com);
        P4 = 0xFF;                      /* pull-ups on for the row inputs */
        P4_LED_KEY = 0xFF;              /* P4 -> current-mode key inputs */
        P4_MOD_OC = 0xFF;               /* disable push-pull output */
        _nop_(); _nop_(); _nop_(); _nop_(); _nop_(); _nop_();
        _nop_(); _nop_(); _nop_(); _nop_(); _nop_(); _nop_();
        _nop_(); _nop_(); _nop_(); _nop_(); _nop_(); _nop_();
        _nop_(); _nop_(); _nop_(); _nop_(); _nop_(); _nop_();

        keyboard_isr_col(idx, P4);

        P4_MOD_OC = 0x00;               /* back to push-pull for the PWM sink */
        P4_LED_KEY = 0x00;
    }
    LED_PWM_OE = 0xFF;
    P0 = 0xFF;
    P3 = 0xFF;

    led_irqs++;
}

void led_task(void)
{
    UINT8 i, col;

    /* Render the next frame as soon as the engine has taken the previous one.
     * The LED frame (~11 ms) is the natural rate, so this settles at one render
     * per published frame and never wastes a frame. */
    if (!frame_ready) {
        /* Direct mode entered but the host has not sent a single colour yet:
         * the colour array is still whatever the last mode left there (all
         * zero after boot), and painting it blanked the whole board until the
         * host got round to pushing its first SET_LEDS -- the "lights go black
         * for a second" glitch.  Stop publishing instead, so the engine keeps
         * streaming the frame that is already on screen. */
        if (led_paused()) {
            return;
        }
        /* One time stamp for the whole frame: every column is drawn at the
         * same animation step even if the engine interrupts mid-render.  The
         * clock is 16 bit and the ISR advances it, so latch it with interrupts
         * off to avoid a torn value. */
        EA = 0;
        render_t = led_t;
        render_chan = chan_sel();
        render_t0 = ((UINT16)TH1 << 8) | TL1;
        EA = 1;

        /* One multiply for the whole frame; poke() then just adds to this
         * pointer. */
        frame = &led_buf[led_work][0];
        rear_state(render_t);

        for (col = 0; col < LED_COLS; col++) {
            if (led_boot) {
                render_boot_col(col);
            } else {
                render_col(col);
            }
        }

        /* Frames are ~11 ms apart, so this fits in a 16-bit tick count; the
         * shift turns 0.5 us ticks into ~1 ms units. */
        EA = 0;
        render_dt = (UINT16)((((UINT16)TH1 << 8) | TL1) - render_t0);
        EA = 1;
        render_ms = (UINT8)(render_dt >> 11);

        if (led_work) {
            phase1 = (UINT8)render_t;
        } else {
            phase0 = (UINT8)render_t;
        }
        frame_ready = 1;
    }

    if (++led_div < 10) {
        return;
    }
    led_div = 0;

    if (led_boot) {
        led_boot_ms = (UINT16)(led_boot_ms + 10);
        if (led_boot_ms >= led_boot_len) {
            led_boot = 0;
        }
    }

    /* decay the reactive flashes, once every 10 ms */
    for (i = 0; i < LED_COLS * LED_ROWS; i++) {
        UINT8 f = led_fade[i];
        if (f > 20) {
            led_fade[i] = (UINT8)(f - 20);
        } else if (f) {
            led_fade[i] = 0;
        }
    }

    if (led_back_fade > 26) {
        led_back_fade = (UINT8)(led_back_fade - 26);
    } else {
        led_back_fade = 0;
    }
}

void led_key_event(UINT8 col, UINT8 row)
{
    if (col < LED_COLS && row < LED_ROWS) {
        led_fade[(UINT8)(col * LED_ROWS + row)] = 255;
    }
    led_back_fade = 255;
}

/*
 * Diagnostics for tools/led-capture.py.  26 bytes:
 *
 *   0  layout version (3)
 *   1  led_irqs            ISR entries, wraps every 256 commons
 *   2  led_frames low      frames published since boot
 *   3  led_frames high
 *   4  led_dma low         DMA base the engine is streaming from
 *   5  led_dma high
 *   6  led_show            buffer on screen
 *   7  led_work            buffer the renderer is filling
 *   8  frame_ready         renderer has a whole frame waiting
 *   9  led_is_on           software "lights on" latch
 *  10  led_back            rear strip enabled
 *  11  led_brightness      0..127
 *  12  led_effect
 *  13  led_speed
 *  14  openrgb_direct_active
 *  15  openrgb_have_colors
 *  16  led_boot            start-up sweep still running
 *  17  led_t low           animation clock
 *  18  led_t high
 *  19  led_color[0]        red of the onboard colour
 *  20  render_ms          time the last frame took to draw, ~1 ms units
 *  21  shown_phase        animation phase of the frame on screen
 *  22  reset cause        PCON reset flag: 0 software, 1 power-on,
 *                         2 watchdog, 3 RST# pin
 *  23  settings_writes    completed save rounds since boot
 *  24  settings_rom_status  0 ok, 1 bad address, 2 command error,
 *                           0xFF no operation since reset
 *  25  settings_block_valid 1 if the settings block verified at boot
 *  26  settings_flags     bit0..3 queued sections
 *  27  save_timer low     ticks left on the save debounce
 *  28  save_timer high
 *  29  settings_rounds   save loop entries, saturating
 *  30  settings_shadow_hits  live values that differed from the snapshot
 *  31  settings_shape   write shape that verified: 0xFF unknown, 0 one
 *                       window per page, 1 a window per byte
 *
 * Everything is latched with interrupts off so the 16-bit values cannot tear,
 * but the critical section is only a handful of moves.
 */
UINT8 led_diag(UINT8 *out) __reentrant
{
    UINT16 frames, t, dma;
    UINT8 show, work, ready, on, boot, phase, o;

    EA = 0;
    dma = led_dma;
    show = led_show;
    work = led_work;
    ready = frame_ready ? 1 : 0;
    frames = led_frames;
    t = led_t;
    on = led_is_on ? 1 : 0;
    boot = led_boot ? 1 : 0;
    EA = 1;
    phase = show ? phase1 : phase0;

    o = 0;
    out[o++] = 5;                /* layout 5 adds the write shape in use */
    out[o++] = led_irqs;
    out[o++] = (UINT8)frames;
    out[o++] = (UINT8)(frames >> 8);
    out[o++] = (UINT8)dma;
    out[o++] = (UINT8)(dma >> 8);
    out[o++] = show;
    out[o++] = work;
    out[o++] = ready;
    out[o++] = on;
    out[o++] = led_back;
    out[o++] = led_brightness;
    out[o++] = led_effect;
    out[o++] = led_speed;
#ifdef OPENRGB
    out[o++] = openrgb_direct_active;
    out[o++] = openrgb_have_colors;
#else
    out[o++] = 0;
    out[o++] = 0;
#endif
    out[o++] = boot;
    out[o++] = (UINT8)t;
    out[o++] = (UINT8)(t >> 8);
    out[o++] = led_color[0];
    out[o++] = render_ms;
    out[o++] = phase;
    /* Why the chip last started.  PCON's two read-only flag bits survive the
     * reset that set them, so the host can tell a power-on from a software
     * jump, a watchdog timeout or the RST# pin without guessing from an
     * uptime counter that wraps. */
    out[o++] = (UINT8)(PCON & MASK_RST_FLAG);
    /* Save health, so a block that will not verify can be told apart from a
     * controller that rejected the address. */
    out[o++] = settings_writes;
    out[o++] = settings_rom_status;
    out[o++] = settings_block_valid;
    /* Layout 4: why a save is not running at all, as opposed to running and
     * failing.  flags says which sections are queued, timer is the debounce
     * countdown and the two counters say whether the loop and the change
     * detector were ever entered. */
    out[o++] = settings_flags();
    t = settings_save_timer();
    out[o++] = (UINT8)t;
    out[o++] = (UINT8)(t >> 8);
    out[o++] = settings_rounds;
    out[o++] = settings_shadow_hits;
    /* Layout 5: which write shape the save path settled on.  0xFF means it
     * has not managed a verified block yet. */
    out[o++] = settings_shape;
    return o;
}

/*
 * Copy one 32-byte common out of the frame that is on screen.  The ISR never
 * writes that buffer, but a publish in the middle of the copy turns it into
 * the renderer's work buffer, so the copy is retried when led_show moved.
 * Returns the buffer number, or 0x80 for an out-of-range common.
 */
UINT8 led_dump(UINT8 col, UINT8 *out) __reentrant
{
    UINT8 i, n, tries;
    UINT16 off;

    if (col >= LED_COLS) {
        return 0x80;
    }
    off = (UINT16)((UINT16)col << 5);
    n = 0;
    for (tries = 0; tries < 3; tries++) {
        n = led_show;
        for (i = 0; i < LED_REC; i++) {
            /* The offset has to stay 16 bit: an (UINT8) cast here truncated
             * every common past the eighth back onto common 0, so dumping
             * commons 8..15 returned common 0's data. */
            out[i] = led_buf[n][(UINT16)(off + i)];
        }
        if (n == led_show) {
            return n;
        }
    }
    return n;
}

/*
 * One 60-byte picture of the frame that is on screen, for the host-side
 * capture tool.  Per common: the brightest key channel's intensity and its
 * RGB, so a jump in brightness *or* in colour is visible from a single
 * request instead of a 14-request sweep.  Real RGB rather than a coarse
 * fingerprint: a smooth animation moves each component by a couple of counts
 * between samples, and the host can tell that apart from a snap.
 *
 * Only the five key channels (1..5) are considered.  Channels 0, 6 and 7 are
 * the spare slots the back light occupies, and they are permanently white at
 * led_back_level; including them made every dim effect report white -
 * Heatmap's cold keys are blue at intensity 25, which a spare slot at 48
 * simply outvoted.
 *
 *   [0 .. 55]  column 0..13: intensity, r, g, b  (matrix columns only: the two
 *              back-light commons are static white and only matter to the
 *              panel, not to the animation the host is checking)
 *   [56]       animation phase of the displayed frame
 *   [57]       led_t low byte (the clock the next frame will use)
 *   [58]       buffer number on screen
 *   [59]       render_ms of the last frame
 */
UINT8 led_sig(UINT8 *out) __reentrant
{
    UINT8 c, ch, best, best_ch, o, n, tries;
    __xdata UINT8 *p;
    UINT16 base;

    /* led_show flips inside the LED interrupt, so a snapshot taken while it
     * changes mixes bytes from two frames.  Copy again when the displayed
     * buffer moved: the body is 28 bytes and a flip lands in it less than one
     * percent of the time, so the retry almost never runs. */
    o = 0;
    for (tries = 0; tries < 3; tries++) {
        n = led_show;
        p = &led_buf[n][0];
        o = 0;
        for (c = 0; c < MATRIX_COLS; c++) {
            UINT8 r, g, b;
            UINT16 off;

            base = (UINT16)((UINT16)c << 5);
            best = 0;
            best_ch = 1;
            for (ch = 1; ch <= 5; ch++) {
                UINT8 v;

                /* A channel with no key in it is a back-light spare slot, not a
                 * key: it carries led_back_level white and would win every
                 * comparison on a dim effect.  Skip it. */
                if (keymap_rowkey(c, (UINT8)(ch - 1)) == 0xFF) {
                    continue;
                }
                v = p[(UINT16)(base + ((UINT16)ch << 2))];

                if (v > best) {
                    best = v;
                    best_ch = ch;
                }
            }
            off = (UINT16)(base + ((UINT16)best_ch << 2));
            r = p[off + 1];
            g = p[off + 2];
            b = p[off + 3];

            out[o++] = best;
            out[o++] = r;
            out[o++] = g;
            out[o++] = b;
        }
        out[o++] = n ? phase1 : phase0;
        out[o++] = (UINT8)led_t;
        out[o++] = n;
        out[o++] = render_ms;
        if (led_show == n) {
            break;
        }
    }
    return o;
}

void led_next_effect(void)
{
    led_effect = (UINT8)((led_effect + 1) % LED_FX_USER_COUNT);
    led_is_on = 1;
}

void led_prev_effect(void)
{
    led_effect = (UINT8)((led_effect + LED_FX_USER_COUNT - 1) % LED_FX_USER_COUNT);
    led_is_on = 1;
}

void led_brightness_up(void)
{
    if (led_brightness >= 127) {
        return;
    }
    if (led_brightness >= 112) {
        led_brightness = 127;
    } else {
        led_brightness = (UINT8)(led_brightness + 16);
    }
    led_is_on = 1;
}

void led_brightness_down(void)
{
    if (led_brightness >= 16) {
        led_brightness = (UINT8)(led_brightness - 16);
    } else {
        led_brightness = 0;         /* dimmed all the way to off */
    }
    led_is_on = 1;
}

void led_brightness_step(void)
{
    led_brightness_up();
}

/* Channel-walk calibration: trim the channel that is currently lit.
 * 128 is unity; up to 255 (2x) and down to 0 (dark). */
void led_gain_step(UINT8 up)
{
    UINT8 ch = render_chan;

    if (up) {
        led_gain[ch] = (led_gain[ch] > 239) ? 255 : (UINT8)(led_gain[ch] + 16);
    } else {
        led_gain[ch] = (led_gain[ch] < 16) ? 0 : (UINT8)(led_gain[ch] - 16);
    }
    led_is_on = 1;
}

void led_speed_up(void)
{
    if (led_speed < 8) {
        led_speed++;
    }
    led_is_on = 1;
}

void led_speed_down(void)
{
    if (led_speed > 1) {
        led_speed--;
    }
    led_is_on = 1;
}

void led_back_toggle(void)
{
    led_back = led_back ? 0 : 1;
    led_is_on = 1;
}

/* Used by the OpenRGB handler and by settings load: make sure the renderer is
 * running before colours are handed to it. */
void led_enable(void)
{
    led_is_on = 1;
}

void led_toggle(void)
{
    led_is_on = !led_is_on;
}
