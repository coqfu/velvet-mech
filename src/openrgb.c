/*
 * OpenRGB "QMK keyboard protocol" revision D.
 *
 * This is the whole surface OpenRGB's QMK driver uses, so the keyboard shows
 * up as a normal per-key device with two zones:
 *
 *   Keyboard  - the 63 key backlights, keylight flag, named after their keys
 *   Underglow - the six rear / side LEDs, underglow flag
 *
 * Packet layout, as the host sees it (hidapi strips the leading report id, so
 * payload byte 0 is the command and the host reads replies from byte 1):
 *
 *   request  [0] command, [1..] parameters
 *   reply    [0] command, [1..] data, [63] end-of-message
 *
 * LED records in a GET_LED_INFO reply are seven bytes each, the first one
 * starting at byte 1:
 *
 *   [0] x        [1] y       [2] flags   [3] red
 *   [4] green    [5] blue    [6] keycode
 *
 * The QMK driver reads them with an offset of 1 + led_index * 7, and treats
 * flags == OPENRGB_FAILURE as "this LED does not exist".
 *
 * Colours land in openrgb_colors[], indexed exactly like keymap_rowkey() for
 * the keys and 63..68 for the strip, and the LED renderer reads them while
 * openrgb_direct_active is set.
 */
#include "ch555.h"
#include "openrgb.h"
#include "usb.h"
#include "led.h"
#include "keymap.h"
#include "keyboard.h"
#include "settings.h"
#include "macro.h"
#include "board.h"

/* Command ids, from OpenRGB's QMKOpenRGBController.h. */
#define CMD_GET_PROTOCOL_VERSION   1
#define CMD_GET_QMK_VERSION        2
#define CMD_GET_DEVICE_INFO        3
#define CMD_GET_MODE_INFO          4
#define CMD_GET_LED_INFO           5
#define CMD_GET_ENABLED_MODES      6
#define CMD_SET_MODE               7
#define CMD_SET_SINGLE_LED         8
#define CMD_SET_LEDS               9

#define RESP_FAILURE        25
#define RESP_SUCCESS        50
#define RESP_END_OF_MESSAGE 100

/* Private commands outside OpenRGB's range.  They only touch this firmware's
 * own settings, so an OpenRGB host never sees them. */
#define CMD_SET_STATE       0x70    /* effect h s v speed save            */
#define CMD_GET_STATE       0x71
#define CMD_GET_REMAP       0x72    /* first count -> (index, usage) ...  */
#define CMD_SET_REMAP       0x73    /* index usage                        */
#define CMD_GET_CUSTOM      0x74    /* first count -> (index, r, g, b)... */
#define CMD_SET_CUSTOM      0x75    /* n, (index, r, g, b) * n            */
#define CMD_SAVE_ALL        0x76    /* persist settings, remap, picture   */
#define CMD_GET_MACRO       0x77    /* slot -> key len (usage hold) ...   */
#define CMD_SET_MACRO       0x78    /* slot key len (usage hold) ...      */
#define CMD_SET_CONFIG      0x79    /* debounce gaming back level cal rgb
                                     * boot mode pace back mode          */
#define CMD_BOOTLOADER      0x7A    /* reboot into the ISP bootloader     */
#define CMD_BOOT_PREVIEW    0x7B    /* replay the start-up animation      */
#define CMD_FLASH_TEST      0x7C    /* data-flash write self-test         */
#define CMD_FLASH_READ      0x6F    /* page -> 62 bytes, read only        */
#define CMD_FORCE_SAVE      0x6D    /* write the settings block now       */

/* Bit 7 of the "save" byte of CMD_SET_MODE.  OpenRGB's driver always sends a
 * plain 0/1 there and sends val = 0 when it selects Direct, so the value byte
 * cannot be treated as a dimmer unconditionally.  The app sets this flag when
 * it wants Direct's val byte to move led_brightness, which is what its Levels
 * slider means.  Bit 0 is still the persistence flag. */
#define SET_FLAG_DIMMER     0x80

/* QMK mode numbers.  Only the ones this firmware can really render are
 * advertised, so OpenRGB never offers a mode that does nothing. */
#define MODE_DIRECT         1
#define MODE_SOLID          2
#define MODE_BREATHING      6
#define MODE_CYCLE_ALL      13
#define MODE_HUE_WAVE       28
#define MODE_SOLID_REACTIVE 32

/* OpenRGB does not send the QMK number back: it numbers the modes it built by
 * their position in this list (1, 2, 3 ...), with Direct assigned last and
 * then shown first in its UI.  So five advertised effects give Direct the
 * number 6.  These positions must be changed whenever mode_list changes. */
#define MODE_POS_STATIC     1
#define MODE_POS_BREATHING  2
#define MODE_POS_CYCLE_ALL  3
#define MODE_POS_HUE_WAVE   4
#define MODE_POS_REACTIVE   5
#define MODE_POS_DIRECT     6

#define LEDS_PER_INFO_PACKET 8      /* 1 + 8 * 7 = 57 bytes */

__xdata UINT8 openrgb_direct_active = 0;
__xdata UINT8 openrgb_have_colors = 0;
__xdata UINT8 openrgb_colors[OPENRGB_LED_COUNT][3];

static __xdata UINT8 tx[OPENRGB_PACKET_SIZE];

/* Last colour the host asked for, so GET_MODE_INFO can report it back. */
static __xdata UINT8 host_hue = 20;
static __xdata UINT8 host_sat = 255;

/* Key positions in OpenRGB's coordinate space, in the same row-major order as
 * the K_* enum, so the board looks like the keyboard in the OpenRGB UI. */
static __code const UINT8 key_x[OPENRGB_KEY_COUNT] = {
      7,  22,  37,  52,  67,  82,  97, 112, 127, 142, 157, 172, 187, 209,
     11,  30,  45,  60,  75,  90, 105, 119, 134, 149, 164, 179, 194, 213,
     13,  34,  49,  63,  78,  93, 108, 123, 138, 153, 168, 183, 207,
     17,  41,  56,  71,  86, 101, 116, 131, 146, 161, 175, 190, 211,
      9,  28,  47, 101, 155, 172, 187, 202, 217
};

static __code const UINT8 key_y[OPENRGB_KEY_COUNT] = {
     22,  22,  22,  22,  22,  22,  22,  22,  22,  22,  22,  22,  22,  22,
     67,  67,  67,  67,  67,  67,  67,  67,  67,  67,  67,  67,  67,  67,
    112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112,
    157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157,
    202, 202, 202, 202, 202, 202, 202, 202, 202
};

/* Underglow strip: one row of six, below the bottom key row, spread across the
 * width so OpenRGB draws it as a long strip under the keyboard. */
#define UG_Y 240
static __code const UINT8 ug_x[OPENRGB_UNDERGLOW_COUNT] = {
    30, 60, 90, 120, 150, 180
};

static __code const char openrgb_name[]   = "Velvet Mech";
static __code const char openrgb_vendor[] = "velvet.mech";
static __code const char openrgb_qmkver[] = "0.22.0-velvet";

/* Modes offered to the host, terminated by zero.  Direct must be in the list:
 * that is the mode OpenRGB uses for its per-LED editor. */
static __code const UINT8 mode_list[] = {
    MODE_DIRECT, MODE_SOLID, MODE_BREATHING,
    MODE_CYCLE_ALL, MODE_HUE_WAVE, MODE_SOLID_REACTIVE, 0
};

static UINT8 scale(UINT8 v, UINT8 s)
{
    return (UINT8)(((UINT16)v * s) >> 8);
}

/* hue 0..255, sat/val 0..255 -> rgb.  Same shape as the renderer's hue2rgb. */
static void hsv2rgb(UINT8 h, UINT8 s, UINT8 v, UINT8 *r, UINT8 *g, UINT8 *b)
{
    UINT8 region, rem, p, q, t;

    if (s == 0) {
        *r = v; *g = v; *b = v;
        return;
    }
    region = (UINT8)(h / 43);
    rem = (UINT8)((UINT8)(h - region * 43) * 6);
    p = scale(v, (UINT8)(255 - s));
    q = scale(v, (UINT8)(255 - (UINT8)(((UINT16)s * rem) >> 8)));
    t = scale(v, (UINT8)(255 - (UINT8)(((UINT16)s * (255 - rem)) >> 8)));

    switch (region) {
    case 0:  *r = v; *g = t; *b = p; break;
    case 1:  *r = q; *g = v; *b = p; break;
    case 2:  *r = p; *g = v; *b = t; break;
    case 3:  *r = p; *g = q; *b = v; break;
    case 4:  *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

/* OpenRGB mode position for the effect the keyboard is showing.  Zero means
 * "not one of OpenRGB's modes"; the host then highlights Direct, which is the
 * closest thing it has to an unnamed picture. */
static UINT8 mode_for_effect(void)
{
    if (openrgb_direct_active) {
        return MODE_POS_DIRECT;
    }
    switch (led_effect) {
    case LED_FX_SOLID:    return MODE_POS_STATIC;
    case LED_FX_BREATH:   return MODE_POS_BREATHING;
    case LED_FX_RAINBOW:  return MODE_POS_CYCLE_ALL;
    case LED_FX_WAVE:     return MODE_POS_HUE_WAVE;
    case LED_FX_REACTIVE: return MODE_POS_REACTIVE;
    default:              return 0;
    }
}

/* Effect for an OpenRGB mode position.  0xFF keeps the current effect, which
 * is what happens for positions we do not advertise. */
static UINT8 effect_for_mode(UINT8 mode)
{
    if (mode >= LED_MODE_EXTRA) {
        UINT8 fx = (UINT8)(mode - LED_MODE_EXTRA);

        return (fx < LED_FX_USER_COUNT) ? fx : 0xFF;
    }
    switch (mode) {
    case MODE_POS_STATIC:    return LED_FX_SOLID;
    case MODE_POS_BREATHING: return LED_FX_BREATH;
    case MODE_POS_CYCLE_ALL: return LED_FX_RAINBOW;
    case MODE_POS_HUE_WAVE:  return LED_FX_WAVE;
    case MODE_POS_REACTIVE:  return LED_FX_REACTIVE;
    default:                 return 0xFF;
    }
}

static void clear_tx(void)
{
    UINT8 i;

    for (i = 0; i < OPENRGB_PACKET_SIZE; i++) {
        tx[i] = 0;
    }
}

static void put_string(UINT8 at, const char *s)
{
    while (*s && at < (OPENRGB_PACKET_SIZE - 1)) {
        tx[at++] = (UINT8)*s++;
    }
    tx[at] = 0;
}

/* Send the assembled packet.  Data always starts at byte 1, the command echo
 * at byte 0, exactly where the host looks for it. */
static void reply(void)
{
    tx[OPENRGB_PACKET_SIZE - 1] = RESP_END_OF_MESSAGE;
    raw_hid_send(tx);
}

static void set_led(UINT8 led, UINT8 r, UINT8 g, UINT8 b)
{
    openrgb_colors[led][0] = r;
    openrgb_colors[led][1] = g;
    openrgb_colors[led][2] = b;
    /* The array is now a real picture, so the renderer may publish it even
     * before the host has filled in every LED. */
    openrgb_have_colors = 1;
}

void openrgb_leave_direct(void)
{
    openrgb_direct_active = 0;
    /* Dropping the flag too means that entering Direct again shows the
     * onboard effect until the host pushes its first colour, instead of
     * flashing the stale picture from the previous Direct session. */
    openrgb_have_colors = 0;
}

void openrgb_task(void)
{
    while (usb_raw_ready()) {
        /* One reply at a time.  The EP3 buffer holds a single 64-byte report,
         * so answering the next command before the host has collected the
         * previous answer replaced it in place and the host read the answer to
         * a different request (or missed one entirely).  The ring keeps the
         * command until the next tick instead. */
        if (usb_raw_tx_busy()) {
            break;
        }
        openrgb_receive((UINT8 *)usb_raw_packet());
        usb_raw_done();
    }
}

/* Reentrant so its locals live on the stack: internal data RAM is full
 * and this is the only large frame left in the module. */
void openrgb_receive(UINT8 *data) __reentrant
{
    UINT8 shift, cmd, i;

    /* hidapi writes report id 0 in front of the 64-byte report and the USB
     * core strips it, but some hosts put it inside the report instead, so
     * accept a single leading zero.  Command 0 does not exist. */
    shift = (data[0] == 0) ? 1 : 0;
    cmd = data[shift];

    clear_tx();

    switch (cmd) {
    case CMD_GET_PROTOCOL_VERSION:
        tx[0] = CMD_GET_PROTOCOL_VERSION;
        tx[1] = OPENRGB_PROTOCOL_VERSION;
        reply();
        break;

    case CMD_GET_QMK_VERSION:
        tx[0] = CMD_GET_QMK_VERSION;
        put_string(1, openrgb_qmkver);
        reply();
        break;

    case CMD_GET_DEVICE_INFO:
        tx[0] = CMD_GET_DEVICE_INFO;
        tx[1] = OPENRGB_LED_COUNT;      /* total number of LEDs            */
        tx[2] = OPENRGB_LED_COUNT;      /* and with empty space reserved   */
        put_string(3, openrgb_name);
        i = 3;
        while (tx[i]) {
            i++;
        }
        put_string((UINT8)(i + 1), openrgb_vendor);
        reply();
        break;

    case CMD_GET_MODE_INFO:
        tx[0] = CMD_GET_MODE_INFO;
        tx[1] = mode_for_effect();
        tx[2] = (UINT8)(((led_speed - 1) << 5) | 0x10);   /* 0..255 */
        tx[3] = host_hue;
        tx[4] = host_sat;
        tx[5] = (UINT8)(led_brightness << 1);             /* 0..254 */
        reply();
        break;

    case CMD_GET_LED_INFO: {
        UINT8 first = data[shift + 1];
        UINT8 count = data[shift + 2];

        if (first > OPENRGB_LED_COUNT) {
            first = OPENRGB_LED_COUNT;
        }
        if (count > LEDS_PER_INFO_PACKET) {
            count = LEDS_PER_INFO_PACKET;
        }
        tx[0] = CMD_GET_LED_INFO;
        for (i = 0; i < count; i++) {
            UINT8 led = (UINT8)(first + i);
            UINT8 o = (UINT8)(1 + i * 7);

            if (led >= OPENRGB_LED_COUNT) {
                /* flags == OPENRGB_FAILURE marks a hole in the LED list */
                tx[o + 2] = RESP_FAILURE;
                continue;
            }
            if (led < OPENRGB_KEY_COUNT) {
                tx[o]     = key_x[led];
                tx[o + 1] = key_y[led];
                tx[o + 2] = OPENRGB_FLAG_KEYLIGHT;
                tx[o + 6] = keymap_usage(0, led);
            } else {
                tx[o]     = ug_x[led - OPENRGB_KEY_COUNT];
                tx[o + 1] = UG_Y;
                tx[o + 2] = OPENRGB_FLAG_UNDERGLOW;
                tx[o + 6] = 0;          /* unnamed strip LED */
            }
            tx[o + 3] = openrgb_colors[led][0];
            tx[o + 4] = openrgb_colors[led][1];
            tx[o + 5] = openrgb_colors[led][2];
        }
        reply();
        break;
    }

    case CMD_GET_ENABLED_MODES:
        tx[0] = CMD_GET_ENABLED_MODES;
        for (i = 1; mode_list[i - 1]; i++) {
            tx[i] = mode_list[i - 1];
        }
        tx[i] = 0;                      /* list terminator */
        reply();
        break;

    case CMD_SET_MODE: {
        /* hue, saturation, value, mode, speed, save - the colour is HSV, not
         * RGB, which is why it has to be converted for the effects. */
        UINT8 h = data[shift + 1];
        UINT8 s = data[shift + 2];
        UINT8 v = data[shift + 3];
        UINT8 mode = data[shift + 4];
        UINT8 speed = data[shift + 5];
        UINT8 save = data[shift + 6];
        UINT8 eff;

        if (mode == MODE_POS_DIRECT) {
            /* OpenRGB's QMK driver sends val = 0 when it selects Direct, so
             * the value byte is only a dimmer when SET_FLAG_DIMMER says so.
             * The colours carry their own brightness; without the flag the
             * board keeps whatever led_brightness the last effect or the Fn
             * keys left, which is what makes OpenRGB's Direct selection safe. */
            if (save & SET_FLAG_DIMMER) {
                led_brightness = (UINT8)(v >> 1);
                if (v && !led_brightness) {
                    led_brightness = 1;
                }
            }
            openrgb_direct_active = 1;
        } else {
            openrgb_direct_active = 0;
            openrgb_have_colors = 0;
            eff = effect_for_mode(mode);
            if (eff != 0xFF) {
                led_effect = eff;
            }
            /* The effects apply the dimmer themselves, so the colour is
             * converted at full value and only the brightness byte drives
             * led_brightness.  Converting at `v` as well would square the
             * attenuation and make host-set colours much darker than the
             * onboard ones. */
            hsv2rgb(h, s, 255, &led_color[0], &led_color[1], &led_color[2]);
            host_hue = h;
            host_sat = s;
            led_brightness = (UINT8)(v >> 1);
            if (v && !led_brightness) {
                led_brightness = 1;
            }
        }
        led_speed = (UINT8)(1 + (speed >> 5));  /* 0..255 -> 1..8 */
        led_enable();
        /* Host-driven brightness is not written to the data flash unless the
         * host asks for it: OpenRGB moves the value while the user drags a
         * slider, and every step would otherwise cost an erase cycle. */
        settings_sync();
        if (save & 0x01) {
            settings_touch();
        }
        tx[0] = CMD_SET_MODE;
        tx[1] = RESP_SUCCESS;
        reply();
        break;
    }

    case CMD_SET_SINGLE_LED: {
        UINT8 led = data[shift + 1];

        tx[0] = CMD_SET_SINGLE_LED;
        if (led < OPENRGB_LED_COUNT) {
            set_led(led, data[shift + 2], data[shift + 3], data[shift + 4]);
            openrgb_direct_active = 1;
            led_enable();
            tx[1] = RESP_SUCCESS;
        } else {
            tx[1] = RESP_FAILURE;
        }
        reply();
        break;
    }

    case CMD_SET_LEDS: {
        UINT8 n = data[shift + 1];

        /* (64 - 2) / 4 = 15 whole entries per packet. */
        if (n > 15) {
            n = 15;
        }
        for (i = 0; i < n; i++) {
            UINT8 o = (UINT8)(shift + 2 + i * 4);
            UINT8 led = data[o];

            if (led < OPENRGB_LED_COUNT) {
                set_led(led, data[o + 1], data[o + 2], data[o + 3]);
            }
        }
        if (n) {
            openrgb_direct_active = 1;
            led_enable();
        }
        /* no reply, exactly like the QMK module */
        break;
    }

    case CMD_SET_STATE: {
        /* effect, hue, sat, value, speed, save.  One packet for the whole
         * state, so the host never leaves the board between two effects. */
        UINT8 effect = data[shift + 1];
        UINT8 h = data[shift + 2];
        UINT8 s = data[shift + 3];
        UINT8 v = data[shift + 4];
        UINT8 speed = data[shift + 5];
        UINT8 save = data[shift + 6];

        if (effect < LED_FX_USER_COUNT) {
            openrgb_direct_active = 0;
            openrgb_have_colors = 0;
            led_effect = effect;
            hsv2rgb(h, s, 255, &led_color[0], &led_color[1], &led_color[2]);
            host_hue = h;
            host_sat = s;
            led_brightness = (UINT8)(v >> 1);
            if (v && !led_brightness) {
                led_brightness = 1;
            }
        }
        led_speed = (UINT8)(1 + (speed >> 5));
        led_enable();
        settings_sync();
        if (save & 0x01) {
            settings_touch();
        }
        tx[0] = CMD_SET_STATE;
        tx[1] = (effect < LED_FX_USER_COUNT) ? RESP_SUCCESS : RESP_FAILURE;
        reply();
        break;
    }

    case CMD_GET_STATE:
        tx[0] = CMD_GET_STATE;
        tx[1] = led_effect;
        tx[2] = host_hue;
        tx[3] = host_sat;
        tx[4] = (UINT8)(led_brightness << 1);
        tx[5] = (UINT8)(((led_speed - 1) << 5) | 0x10);
        tx[6] = led_back;
        tx[7] = led_back_level;
        tx[8] = openrgb_direct_active;
        tx[9] = kbd_gaming;
        tx[10] = kbd_debounce;
        tx[11] = led_cal[0];
        tx[12] = led_cal[1];
        tx[13] = led_cal[2];
        tx[14] = led_back_rgb[0];
        tx[15] = led_back_rgb[1];
        tx[16] = led_back_rgb[2];
        tx[17] = led_boot_mode;
        tx[18] = led_boot_pace;
        tx[19] = led_back_mode;
        reply();
        break;

    case CMD_GET_REMAP: {
        UINT8 first = data[shift + 1];
        UINT8 count = data[shift + 2];

        if (first > K_COUNT) {
            first = K_COUNT;
        }
        if (count > 31) {
            count = 31;             /* 1 + 31 * 2 = 63 bytes */
        }
        tx[0] = CMD_GET_REMAP;
        for (i = 0; i < count; i++) {
            UINT8 k = (UINT8)(first + i);

            tx[1 + i * 2] = k;
            tx[2 + i * 2] = (k < K_COUNT) ? keymap_override[k] : 0xFF;
        }
        reply();
        break;
    }

    case CMD_SET_REMAP: {
        UINT8 k = data[shift + 1];
        UINT8 usage = data[shift + 2];

        if (k < K_COUNT) {
            keymap_override[k] = usage;
            settings_touch_map();
            tx[0] = CMD_SET_REMAP;
            tx[1] = RESP_SUCCESS;
        } else {
            tx[0] = CMD_SET_REMAP;
            tx[1] = RESP_FAILURE;
        }
        reply();
        break;
    }

    case CMD_GET_CUSTOM: {
        UINT8 first = data[shift + 1];
        UINT8 count = data[shift + 2];

        if (first >= OPENRGB_KEY_COUNT) {
            first = OPENRGB_KEY_COUNT;
        }
        if (count > 15) {
            count = 15;             /* 1 + 15 * 4 = 61 bytes */
        }
        tx[0] = CMD_GET_CUSTOM;
        for (i = 0; i < count; i++) {
            UINT8 k = (UINT8)(first + i);
            UINT8 o = (UINT8)(1 + i * 4);

            tx[o] = k;
            if (k < OPENRGB_KEY_COUNT) {
                tx[o + 1] = led_custom[k][0];
                tx[o + 2] = led_custom[k][1];
                tx[o + 3] = led_custom[k][2];
            }
        }
        reply();
        break;
    }

    case CMD_SET_CUSTOM: {
        UINT8 n = data[shift + 1];

        if (n > 15) {
            n = 15;
        }
        for (i = 0; i < n; i++) {
            UINT8 o = (UINT8)(shift + 2 + i * 4);
            UINT8 k = data[o];

            if (k < OPENRGB_KEY_COUNT) {
                led_custom[k][0] = data[o + 1];
                led_custom[k][1] = data[o + 2];
                led_custom[k][2] = data[o + 3];
            }
        }
        settings_touch_custom();
        tx[0] = CMD_SET_CUSTOM;
        tx[1] = RESP_SUCCESS;
        reply();
        break;
    }

    case CMD_GET_MACRO: {
        UINT8 slot = data[shift + 1];

        if (slot >= MACRO_SLOTS) {
            tx[0] = CMD_GET_MACRO;
            tx[1] = RESP_FAILURE;
            reply();
            break;
        }
        tx[0] = CMD_GET_MACRO;
        tx[1] = macro_key[slot];
        tx[2] = macro_len[slot];
        for (i = 0; i < MACRO_STEPS; i++) {
            tx[3 + i * 2] = macro_steps[slot][i].usage;
            tx[4 + i * 2] = macro_steps[slot][i].hold;
        }
        reply();
        break;
    }

    case CMD_SET_MACRO: {
        /* One whole slot fits in a packet: 3 header bytes + 24 * 2 = 51. */
        UINT8 slot = data[shift + 1];
        UINT8 n = data[shift + 3];
        UINT8 key = data[shift + 2];

        /* A trigger has to be a key that can be pressed on its own.  Fn is a
         * layer modifier: a macro bound there would fire on every Fn lighting
         * chord, so it is rejected here as well as hidden in the app.  A
         * non-empty macro with an out-of-range key would also sit in the slot
         * looking armed while macro_bound() could never match it. */
        if (slot >= MACRO_SLOTS || n > MACRO_STEPS ||
            (n && (key >= K_COUNT || key == K_FN))) {
            tx[0] = CMD_SET_MACRO;
            tx[1] = RESP_FAILURE;
            reply();
            break;
        }
        macro_stop();
        macro_key[slot] = (key < K_COUNT) ? key : MACRO_UNBOUND;
        macro_len[slot] = n;
        for (i = 0; i < MACRO_STEPS; i++) {
            UINT8 o = (UINT8)(shift + 4 + i * 2);

            if (i < n) {
                macro_steps[slot][i].usage = data[o];
                macro_steps[slot][i].hold = data[o + 1];
            } else {
                macro_steps[slot][i].usage = 0;
                macro_steps[slot][i].hold = 0;
            }
        }
        settings_touch_macro();
        tx[0] = CMD_SET_MACRO;
        tx[1] = RESP_SUCCESS;
        reply();
        break;
    }

    case CMD_SAVE_ALL:
        settings_touch();
        settings_touch_map();
        settings_touch_custom();
        settings_touch_macro();
        /* Write now, then answer.  The old code replied as soon as the save
         * was queued, so the host could report success while the flash still
         * held the previous bytes, and a quick replug lost the change.  The
         * reply now means every queued section read back correctly. */
        tx[0] = CMD_SAVE_ALL;
        tx[1] = settings_save_all_now() ? RESP_SUCCESS : RESP_FAILURE;
        reply();
        break;

    case CMD_BOOTLOADER:
        /* Reboot into the WCH ISP bootloader so the host tool can flash a new
         * image without a physical unplug.
         *
         * bBOOT_LOAD is a read-only bit that hardware sets on a power-on
         * reset, and a software reset clears it, so `GLOBAL_CFG |= bSW_RESET`
         * would come up in the application again.  The bootloader's own code,
         * however, lives at 0xF400 and is executable, so the CH55x trick is
         * to jump there directly after stopping everything that could touch
         * the bus: interrupts off, USB detached, then a long branch.
         *
         * The jump is inline assembly in a __naked function: a normal C call
         * would push a return address and the bootloader would try to return
         * into a stack it is about to reset. */
        tx[0] = CMD_BOOTLOADER;
        tx[1] = RESP_SUCCESS;
        reply();
        /* Let the reply drain before the peripheral is torn down.  USB
         * completion is asynchronous, so this is a few frame times rather
         * than a single nop. */
        delay_ms(6);
        boot_enter();
        break;

    case CMD_SET_CONFIG: {
        /* debounce, gaming, back on, back level, cal r/g/b, back r/g/b,
         * start-up mode, start-up pace, rear animation */
        UINT8 deb = data[shift + 1];
        UINT8 gam = data[shift + 2];
        UINT8 bon = data[shift + 3];
        UINT8 blv = data[shift + 4];

        if (deb >= 1 && deb <= 6) {
            kbd_debounce = deb;
        }
        kbd_gaming = gam ? 1 : 0;
        led_back = bon ? 1 : 0;
        led_back_level = (blv > 127) ? 127 : blv;
        led_cal[0] = data[shift + 5];
        led_cal[1] = data[shift + 6];
        led_cal[2] = data[shift + 7];
        led_back_rgb[0] = data[shift + 8];
        led_back_rgb[1] = data[shift + 9];
        led_back_rgb[2] = data[shift + 10];
        if (data[shift + 11] < LED_BOOT_COUNT) {
            led_boot_mode = data[shift + 11];
        }
        if (data[shift + 12] <= 2) {
            led_boot_pace = data[shift + 12];
        }
        if (data[shift + 13] < LED_BACK_COUNT) {
            led_back_mode = data[shift + 13];
        }
        led_enable();
        settings_touch();
        tx[0] = CMD_SET_CONFIG;
        tx[1] = RESP_SUCCESS;
        reply();
        break;
    }

    case CMD_BOOT_PREVIEW:
        /* Replays the animation the user just picked, so the app can promise
         * "this is what power-on looks like" without a replug. */
        led_enable();
        led_boot_preview();
        tx[0] = CMD_BOOT_PREVIEW;
        tx[1] = RESP_SUCCESS;
        reply();
        break;

    case CMD_FORCE_SAVE:
        tx[0] = CMD_FORCE_SAVE;
        tx[1] = settings_force_save() ? RESP_SUCCESS : RESP_FAILURE;
        tx[2] = settings_rom_status;
        reply();
        break;

    case CMD_FLASH_READ: {
        UINT8 page = data[shift + 1];
        UINT8 i;

        if (page < 16) {
            UINT16 base = (UINT16)(0xF000 + (UINT16)page * 64);

            for (i = 0; i < 62; i++) {
                tx[1 + i] = settings_flash_read((UINT16)(base + i));
            }
        }
        tx[0] = CMD_FLASH_READ;
        reply();
        break;
    }

    case CMD_FLASH_TEST:
        settings_selftest(&tx[1]);
        tx[0] = CMD_FLASH_TEST;
        reply();
        break;

    case OPENRGB_CMD_DIAG:
        tx[0] = OPENRGB_CMD_DIAG;
        led_diag(&tx[1]);
        reply();
        break;

    case OPENRGB_CMD_SIG:
        tx[0] = OPENRGB_CMD_SIG;
        led_sig(&tx[1]);
        reply();
        break;

    case OPENRGB_CMD_DUMP: {
        UINT8 n = led_dump(data[shift + 1], &tx[1]);

        tx[0] = OPENRGB_CMD_DUMP;
        tx[33] = n;                 /* buffer number, 0x80 = bad common */
        reply();
        break;
    }

    default:
        break;
    }
}
