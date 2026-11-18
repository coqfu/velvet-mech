#ifndef CH555_KEYMAP_H
#define CH555_KEYMAP_H

#include "ch555.h"

/* Physical key indices, row-major, exactly as the layout was captured. */
enum {
    K_ESC = 0, K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9, K_0,
    K_MINUS, K_EQUAL, K_BSPC,
    K_TAB, K_Q, K_W, K_E, K_R, K_T, K_Y, K_U, K_I, K_O, K_P,
    K_LBRC, K_RBRC, K_BSLS,
    K_CAPS, K_A, K_S, K_D, K_F, K_G, K_H, K_J, K_K, K_L,
    K_SCLN, K_QUOT, K_ENT,
    K_LSFT, K_Z, K_X, K_C, K_V, K_B, K_N, K_M,
    K_COMM, K_DOT, K_SLSH, K_UP, K_RSFT,
    K_LCTL, K_LGUI, K_LALT, K_SPC, K_RALT, K_LEFT, K_DOWN, K_RGHT, K_FN,
    K_COUNT
};

/* Matrix geometry (measured on the real board):
 *   columns: P0.0..P0.7  -> col 0..7
 *            P3.0..P3.5  -> col 8..13
 *   rows:    P4.1..P4.5  (bit 1..5 of P4)
 *   diodes conduct column -> row, so columns are driven low and P4 is read.
 */
#define MATRIX_COLS 14

UINT8 keymap_usage(UINT8 layer, UINT8 key);
UINT16 keymap_media(UINT8 layer, UINT8 key);
UINT8 keymap_rowkey(UINT8 col, UINT8 row);

/* Runtime remap of the base layer, one HID usage per key (0xFF = factory
 * keycode, 0x00 = key disabled).  Loaded from the data flash by settings.c. */
extern __xdata UINT8 keymap_override[K_COUNT];
void keymap_reset_overrides(void);

#endif
