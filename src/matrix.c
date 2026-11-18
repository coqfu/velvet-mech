#include "matrix.h"

/*
 * Every candidate pin is used in quasi-bidirectional mode: a weak pull-up
 * makes it an input, and writing 0 turns it into a strong low driver.
 *
 * Reading while one pin is driven low gives the connectivity of the switch
 * matrix: any line that goes low is bridged to the driven pin. Scanning every
 * pin in turn therefore finds each key regardless of which side is "row" and
 * which is "column", and regardless of diode orientation.
 */

void matrix_init(void)
{
    P4_LED_KEY = 0x00;      /* P4 as ordinary GPIO, not current-mode key input */
    LED_CTRL = 0x00;        /* RGB/LED engine off so it cannot drive pins */
    LED_PWM_OE = 0x00;
    LED_COMMON = 0x00;

    P0_MOD_OC = 0xFF; P0_DIR_PU = 0xFF;
    P1_MOD_OC = 0xFF; P1_DIR_PU = 0xFF;
    P2_MOD_OC = 0xFF; P2_DIR_PU = 0xFF;
    P3_MOD_OC = 0xFF; P3_DIR_PU = 0xFF;
    P4_MOD_OC = 0xFF; P4_DIR_PU = 0xFF;

    P0 = 0xFF; P1 = 0xFF; P2 = 0xFF; P3 = 0xFF; P4 = 0xFF;
}

void matrix_all_high(void)
{
    P0 = 0xFF; P1 = 0xFF; P2 = 0xFF; P3 = 0xFF; P4 = 0xFF;
}

void matrix_settle(void)
{
    UINT8 i = 200;
    while (i--) {
        _nop_();
    }
}

void matrix_drive(UINT8 pin)
{
    UINT8 mask = (UINT8)(1 << (pin & 7));
    switch (pin >> 3) {
    case 0: P0 &= ~mask; break;
    case 1: P1 &= ~mask; break;
    case 2: P2 &= ~mask; break;
    case 3: P3 &= ~mask; break;
    default: P4 &= ~mask; break;
    }
}

UINT8 matrix_port(UINT8 port)
{
    switch (port) {
    case 0: return P0;
    case 1: return P1;
    case 2: return P2;
    case 3: return P3;
    default: return P4;
    }
}
