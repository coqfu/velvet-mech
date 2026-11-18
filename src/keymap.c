#include "keymap.h"

/* HID keyboard usages. 0x00 = nothing. 0xE0..0xE7 are modifiers. */
static __code const UINT8 kbd_layer0[K_COUNT] = {
    /* row 1 */
    0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x2D, 0x2E, 0x2A,
    /* row 2 */
    0x2B, 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13,
    0x2F, 0x30, 0x31,
    /* row 3 */
    0x39, 0x04, 0x16, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F,
    0x33, 0x34, 0x28,
    /* row 4 */
    0xE1, 0x1D, 0x1B, 0x06, 0x19, 0x05, 0x11, 0x10,
    0x36, 0x37, 0x38, 0x52, 0xE5,
    /* row 5 */
    0xE0, 0xE3, 0xE2, 0x2C, 0xE6, 0x50, 0x51, 0x4F, 0x00
};

/* Fn layer: F-keys, navigation, print screen. */
static __code const UINT8 kbd_layer1[K_COUNT] = {
    /* row 1: ESC -> grave, digits -> F1..F12 */
    0x35, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43,
    0x44, 0x45, 0x4C,             /* BSPC -> Delete */
    /* row 2 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x46, 0x47, 0x48,
    0x00, 0x00, 0x00,             /* U -> Ins, I -> PrtSc, O -> ScrLk, P -> Pause */
    /* row 3 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53,
    0x00, 0x00, 0x00,             /* L -> NumLock */
    /* row 4 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, /* UP: RGB brightness, no keystroke */
    /* row 5 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                                  /* LEFT/RGHT: effect speed, DOWN: brightness */
};

/* Consumer usages on the Fn layer (0 = none).
 *   Fn+Q mute, Fn+W vol-, Fn+E vol+, Fn+R prev, Fn+T next, Fn+Y play/pause
 */
static __code const UINT16 media_layer1[K_COUNT] = {
    /* row 1: ESC 1 2 3 4 5 6 7 8 9 0 - = BSPC */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* row 2: TAB Q W E R T Y U I O P [ ] \ */
    0,     0xE2,  0xEA,  0xE9,  0xB6,  0xB5,  0xCD,  0, 0, 0, 0, 0, 0, 0,
    /* row 3: CAPS A S D F G H J K L ; ' ENT */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* row 4: LSFT Z X C V B N M , . / UP RSFT */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* row 5: LCTL LGUI LALT SPC RALT LEFT DOWN RGHT FN */
    0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Per column, the key index of matrix rows P4.1..P4.5 (0xFF = no key). */
static __code const UINT8 row_key[MATRIX_COLS][5] = {
    { K_ESC,  K_TAB,  K_CAPS, K_LSFT, K_LCTL },  /* P0.0 */
    { K_1,    K_Q,    K_A,    K_Z,    K_LGUI },  /* P0.1 */
    { K_2,    K_W,    K_S,    K_X,    K_LALT },  /* P0.2 */
    { K_3,    K_E,    K_D,    K_C,    K_SPC  },  /* P0.3 */
    { K_4,    K_R,    K_F,    K_V,    K_RALT },  /* P0.4 */
    { K_5,    K_T,    K_G,    K_B,    K_LEFT },  /* P0.5 */
    { K_6,    K_Y,    K_H,    K_N,    K_DOWN },  /* P0.6 */
    { K_7,    K_U,    K_J,    K_M,    K_RGHT },  /* P0.7 */
    { K_8,    K_I,    K_K,    K_COMM, 0xFF   },  /* P3.0 */
    { K_9,    K_O,    K_L,    K_DOT,  0xFF   },  /* P3.1 */
    { K_0,    K_P,    K_SCLN, K_SLSH, 0xFF   },  /* P3.2 */
    { K_MINUS,K_LBRC, K_QUOT, K_RSFT, K_FN   },  /* P3.3 */
    { K_EQUAL,K_RBRC, K_ENT,  K_UP,   0xFF   },  /* P3.4 */
    { K_BSPC, K_BSLS, 0xFF,   0xFF,   0xFF   }   /* P3.5 */
};

UINT8 keymap_usage(UINT8 layer, UINT8 key)
{
    if (!layer && keymap_override[key] != 0xFF) {
        return keymap_override[key];
    }
    return layer ? kbd_layer1[key] : kbd_layer0[key];
}

UINT16 keymap_media(UINT8 layer, UINT8 key)
{
    if (!layer && keymap_override[key] != 0xFF) {
        return 0;       /* a remapped key sends its own usage, not media */
    }
    return layer ? media_layer1[key] : 0;
}

UINT8 keymap_rowkey(UINT8 col, UINT8 row)
{
    return row_key[col][row];
}

__xdata UINT8 keymap_override[K_COUNT];

void keymap_reset_overrides(void)
{
    UINT8 i;

    for (i = 0; i < K_COUNT; i++) {
        keymap_override[i] = 0xFF;
    }
}
