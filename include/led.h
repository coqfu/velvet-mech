#ifndef CH555_LED_H
#define CH555_LED_H

#include "ch555.h"

/* Onboard effects.
 *
 * The first six are the ones OpenRGB knows and lists by name; every other one
 * is addressed from the host as mode 100 + effect id (see openrgb.c).  Effects
 * after LED_FX_CUSTOM are diagnostics and are never offered to the host. */
#define LED_FX_OFF        0
#define LED_FX_SOLID      1
#define LED_FX_BREATH     2
#define LED_FX_RAINBOW    3
#define LED_FX_AURORA     4
#define LED_FX_WAVE       5
#define LED_FX_REACTIVE   6

#define LED_FX_SPECTRUM   7      /* hue bands down the rows              */
#define LED_FX_RAINWAVE   8      /* diagonal rainbow                     */
#define LED_FX_COLORWAVE  9      /* single hue rolling with a wave shape */
#define LED_FX_GRADIENT  10      /* vertical brightness ramp             */
#define LED_FX_SPIRAL    11      /* hue swirl                            */
#define LED_FX_CHECKER   12      /* two-state checkerboard               */
#define LED_FX_STROBE    13      /* whole board flash                    */
#define LED_FX_ALTERNATE 14      /* rows alternate two states            */
#define LED_FX_TWINKLE   15      /* scattered sparkles                   */
#define LED_FX_STARLIGHT 16      /* sparse white stars                   */
#define LED_FX_FIRE      17      /* red/orange flicker                   */
#define LED_FX_OCEAN     18      /* blue swell                           */
#define LED_FX_SUNSET    19      /* orange to magenta by row             */
#define LED_FX_VAPOR     20      /* soft pastel drift                    */
#define LED_FX_MATRIX    21      /* green code drift                     */
#define LED_FX_SNOW      22      /* white flakes drifting                */
#define LED_FX_RIPPLE    23      /* rings from the centre                */
#define LED_FX_BREATHRAIN 24     /* breathing rainbow                    */
#define LED_FX_HEARTBEAT 25      /* double-beat pulse                    */
#define LED_FX_SCANNER   26      /* column sweep                         */
#define LED_FX_LASER     27      /* row sweep                            */
#define LED_FX_PULSE     28      /* sharp expanding ring                 */
#define LED_FX_CYCLEROW  29      /* one row at a time                    */
#define LED_FX_RANDOMFADE 30     /* per-key random fades                 */
#define LED_FX_HEATMAP   31      /* keys warm where you type             */
#define LED_FX_RAINREACT 32      /* held keys take the hue wheel         */
#define LED_FX_CANDY     33      /* blocks of candy colour               */
#define LED_FX_CUSTOM    34      /* per-key colours stored on the board  */

#define LED_FX_TEST      35      /* row colours: red green blue white orange */
#define LED_FX_CHAN      36      /* wiring check: one channel lit at a time  */
#define LED_FX_COUNT     37
#define LED_FX_USER_COUNT (LED_FX_CUSTOM + 1)   /* what Fn cycles through */

/* Host-facing onset: effect n is selected with mode (100 + n) unless it has an
 * OpenRGB mode of its own. */
#define LED_MODE_EXTRA   100

/* Start-up animation.  Stored in the settings block, chosen by the host over
 * 0x79 and replayable with 0x7B, so the app can show what it selected without
 * a replug. */
#define LED_BOOT_OFF      0      /* no animation: the effect starts at once */
#define LED_BOOT_SWEEP    1      /* warm diagonal flash sweep               */
#define LED_BOOT_RAINBOW  2      /* diagonal rainbow wipe                   */
#define LED_BOOT_FADE     3      /* whole panel fades up from black         */
#define LED_BOOT_SCAN     4      /* bright column travels across the board  */
#define LED_BOOT_PULSE    5      /* three whole-panel pulses                */
#define LED_BOOT_COUNT    6

/* Rear light animation.  The six rear LEDs are driven from the spare slots and
 * their physical order is not known, so every pattern here is the same for all
 * six: it animates in time rather than pretending to chase along the strip. */
#define LED_BACK_SOLID     0
#define LED_BACK_BREATH    1
#define LED_BACK_HEARTBEAT 2
#define LED_BACK_PULSE     3
#define LED_BACK_RAINBOW   4
#define LED_BACK_REACTIVE  5
#define LED_BACK_STROBE    6      /* hard on/off gate                     */
#define LED_BACK_SPARKLE   7      /* pseudo-random flicker                */
#define LED_BACK_WAVE      8      /* hue drift with a brightness swell    */
#define LED_BACK_COUNT     9

extern UINT8 led_effect;
extern UINT8 led_brightness;    /* 0..127 master level; the engine PWM is 6 bit */
extern UINT8 led_speed;         /* animation rate 1 (slow) .. 8 (fast) */
extern UINT8 led_back;          /* rear / side LEDs on */
extern UINT8 led_back_level;    /* rear / side brightness 0..127 */
extern UINT8 led_back_rgb[3];   /* rear / side colour */
extern UINT8 led_boot_mode;     /* LED_BOOT_*, start-up animation */
extern UINT8 led_boot_pace;     /* 0 fast, 1 normal, 2 slow       */
extern UINT8 led_back_mode;     /* LED_BACK_*, rear animation     */
extern UINT8 led_color[3];
extern __xdata UINT8 led_gain[8];   /* per-channel trim, 128 = unity */
extern UINT8 led_cal[3];            /* white balance per colour, 128 = unity */
extern volatile UINT8 led_irqs;

/* One colour per key, used by LED_FX_CUSTOM.  Key order is keymap's K_*. */
extern __xdata UINT8 led_custom[63][3];

void led_init(void);
void led_shutdown(void);
void led_irq(void);             /* LED inhibition ISR body (22.1 us slot) */
void led_task(void);            /* effect renderer, call once per ms */
/* True while the renderer is holding the frame that is on screen on purpose,
 * so no new frame is published.  main() must not treat that as a broken
 * inhibition scan. */
__bit led_paused(void);

void led_next_effect(void);
void led_prev_effect(void);
void led_brightness_up(void);
void led_brightness_down(void);
void led_brightness_step(void);  /* alias for up, kept for compatibility */
void led_speed_up(void);
void led_speed_down(void);
void led_back_toggle(void);
void led_boot_preview(void);    /* replay the selected start-up animation */
void led_gain_step(UINT8 up);    /* channel-walk calibration */
void led_enable(void);          /* clears the software "lights off" latch */
void led_toggle(void);

/* Called from the key scan when a switch closes; drives the reactive effect. */
void led_key_event(UINT8 col, UINT8 row);

/* Diagnostics used by tools/led-capture.py.  Both run from the main loop.
 * led_diag() writes 32 bytes of engine state and returns that count;
 * led_dump() copies one 32-byte common out of the frame that is on screen and
 * returns its buffer number, or 0x80 if the common number is out of range. */
UINT8 led_diag(UINT8 *out) __reentrant;
UINT8 led_dump(UINT8 col, UINT8 *out) __reentrant;

/* 60-byte snapshot of the displayed frame, one entry per matrix common:
 * the brightest key channel's (intensity, r, g, b), then phase, clock low
 * byte, buffer number, render time.  The back-light spare slots are excluded
 * so a dim effect is not masked by the fixed white rear LEDs. */
UINT8 led_sig(UINT8 *out) __reentrant;

#endif
