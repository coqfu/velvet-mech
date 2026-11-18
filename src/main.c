#include "ch555.h"
#include "board.h"
#include "usb.h"
#include "led.h"
#include "settings.h"
#include "macro.h"

#ifdef DETECT_MODE
#include "matrix.h"
#endif
#ifndef DETECT_MODE
#include "keyboard.h"
#ifdef OPENRGB
#include "openrgb.h"
#endif
#endif

/* SDCC emits the vector table from this module, so all ISRs live here. */
void USB_DeviceInterrupt(void) __interrupt(INT_NO_USB) { usb_irq(); }
void timer0_isr(void) __interrupt(1) { timer0_irq(); }
#ifndef DETECT_MODE
void led_isr(void) __interrupt(INT_NO_PWM_I2C) { led_irq(); }
#endif

#ifdef DETECT_MODE

/*
 * Matrix detector.
 *
 * Every candidate pin is driven low in turn while all the others are pulled
 * up. Whenever a line that was not connected before is pulled low, the pair
 * (drive pin, read pin) is pushed into a queue and sent to the host as an
 * 8-byte vendor HID report:  A5 <drive> <read> 00 00 00 00 00
 *
 * The host-side logger turns the stream into a wiring map; the same pair
 * coordinates are then used by the production firmware, so the order of rows
 * and columns does not matter.
 */

#define QUEUE_LEN 64

static __xdata UINT8 prev[MATRIX_PINS][5];
static __xdata UINT8 queue[QUEUE_LEN][2];
static UINT8 q_head = 0;
static UINT8 q_tail = 0;

static void queue_push(UINT8 d, UINT8 r)
{
    UINT8 next = (UINT8)((q_head + 1) % QUEUE_LEN);
    if (next == q_tail) {
        return;             /* full: drop, the logger will see it again */
    }
    queue[q_head][0] = d;
    queue[q_head][1] = r;
    q_head = next;
}

static void scan_matrix(void)
{
    UINT8 d, p, bit;
    UINT8 cur[5];

    for (d = 0; d < MATRIX_PINS; d++) {
        matrix_all_high();
        matrix_settle();
        matrix_drive(d);
        matrix_settle();

        for (p = 0; p < 5; p++) {
            cur[p] = matrix_port(p);
        }

        for (p = 0; p < 5; p++) {
            UINT8 fresh = (UINT8)(cur[p] & (UINT8)~prev[d][p]);
            for (bit = 0; bit < 8; bit++) {
                UINT8 pin = (UINT8)(p * 8 + bit);
                if ((fresh & (1 << bit)) && pin != d) {
                    queue_push(d, pin);
                }
            }
            prev[d][p] = cur[p];
        }
    }
}

static void drain_queue(void)
{
    UINT8 rep[8];

    while (q_tail != q_head) {
        if (usb_raw_busy()) {
            return;
        }
        rep[0] = 0xA5;
        rep[1] = queue[q_tail][0];
        rep[2] = queue[q_tail][1];
        rep[3] = rep[4] = rep[5] = rep[6] = rep[7] = 0;
        usb_send_raw(rep);
        q_tail = (UINT8)((q_tail + 1) % QUEUE_LEN);
    }
}

void main(void)
{
    clock_init();
    delay_ms(30);

    matrix_init();
    usb_init();
    timer0_init();
    EA = 1;

    while (1) {
        if (tick_1ms) {
            tick_1ms = 0;
            scan_matrix();
        }
        drain_queue();
    }
}

#else

void main(void)
{
    UINT16 ms = 0;
    UINT8 led_ok = 0;
    UINT8 gpio_mode = 0;
    UINT8 no_frame = 0;
    UINT8 last_irqs = 0;
    UINT8 last_seq = 0;

    clock_init();
    delay_ms(30);

    keyboard_init();
    usb_init();
    timer0_init();
    EA = 1;

    /* The RGB engine is only started once the host has configured the
     * device, like WCH's own RGB keyboard demo. */
    while (!usb_ready) {
        ;
    }
    delay_ms(10);
    led_init();
    macro_init();
    settings_load();

    while (1) {
        if (tick_1ms) {
            tick_1ms = 0;
            ms++;

            if (!led_ok) {
                if (led_irqs) {
                    led_ok = 1;
                    ms = 0;
                } else if (ms > 200) {
                    /* The LED engine never produced an inhibition interrupt.
                     * Give it up and scan the matrix as plain GPIO so the
                     * keyboard keeps working. */
                    led_shutdown();
                    gpio_mode = 1;
                    led_ok = 1;
                }
            }

            if (gpio_mode) {
                keyboard_scan_gpio();
            } else if (frame_seq != last_seq) {
                /* Consume exactly one published frame.  Clearing a "ready"
                 * bit before decoding was racy: if the LED ISR published the
                 * next frame during the decode, the flag was already set
                 * again and the same frame was decoded twice on consecutive
                 * ticks.  That advanced the debounce by two frames for one
                 * sample, so a noisy sample could be accepted early. */
                last_seq = keyboard_process();
                no_frame = 0;
            } else if (led_irqs != last_irqs) {
                /* Interrupts arrive but the frame never completes: the
                 * inhibition-slot scan is not usable on this panel, so fall
                 * back to plain GPIO scanning rather than losing the keyboard.
                 *
                 * "No frame" only counts while a frame is actually wanted.
                 * Direct mode deliberately holds the frame on screen until
                 * the host sends its first colour, and reading that pause as
                 * a hardware fault switched the LED engine off for the rest
                 * of the session -- so OpenRGB selecting Direct and taking a
                 * moment over its first packet left the board frozen. */
                last_irqs = led_irqs;
                if (led_paused()) {
                    no_frame = 0;
                } else if (++no_frame >= 60) {
                    led_shutdown();
                    gpio_mode = 1;
                }
            }

            /* Report before the LED render.  Rendering one frame takes about
             * 2 ms, and a key report that waited behind it reached the host
             * 2 ms late.  keyboard_report() clears kbd_dirty itself and only
             * when every endpoint has the new state, so this line doubles as
             * the retry for a report that found the endpoint busy. */
            keyboard_special();
            macro_task();

            if (kbd_dirty || ((ms & 0x07) == 0)) {
                keyboard_report();
            }

            led_task();
            settings_task();
#ifdef OPENRGB
            openrgb_task();
#endif
        }
    }
}

#endif
