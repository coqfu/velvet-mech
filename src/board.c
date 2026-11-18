#include "board.h"

/* 24 MHz internal oscillator (reset default is 24 MHz too, but be explicit). */
void clock_init(void)
{
    SAFE_MOD = 0x55;
    SAFE_MOD = 0xAA;
    CLOCK_CFG = (CLOCK_CFG & ~MASK_SYS_CK_SEL) | 0x05;
}

void delay_ms(UINT16 ms)
{
    while (ms--) {
        UINT16 i = 400;
        while (i--) {
            _nop_();
        }
    }
}

volatile __bit tick_1ms = 0;

/* ------------------------------------------------------------- bootloader --
 *
 * The CH555 ISP bootloader lives at 0xF400.  In practice it cannot be entered
 * from software on this chip: bBOOT_LOAD is read-only, 1 only after a power-on
 * reset, and the bootloader uses it to decide between ISP and the application.
 * A software branch to 0xF400 therefore runs the bootloader with the bit clear
 * and it software-resets straight back here (measured: the board re-enumerates
 * as 1209:c555 and 4348:55e0 never appears).
 *
 * What this routine is still good for is a clean restart: it quiesces the
 * peripherals and drops the USB connection, so the host sees a reconnect
 * instead of a half-initialised device.  Flashing is done by the app, which
 * waits for the bootloader after the board has been replugged.
 *
 * Everything has to be quiesced first.  The USB interrupt would otherwise fire
 * while the bootloader re-initialises the same peripheral, and an LED frame
 * interrupt would keep writing a buffer the bootloader reuses.  So the order
 * is: mask interrupts, disable USB events, drop the DP pull-up so the host
 * sees a disconnect, settle, then a long jump.
 *
 * The USB registers are memory-mapped (XBYTE), not SFRs, so they are cleared
 * through DPTR - `mov _USB_CTRL, #0` does not assemble.
 *
 * __naked keeps SDCC from emitting a prologue that pushes a return address:
 * there is nothing to return to, and the bootloader resets SP on entry.
 */
void boot_enter(void) __naked
{
    __asm
        clr  ea                 ; no interrupt after this point
        mov  dptr, #0x223B      ; USB_IE = 0
        clr  a
        movx @dptr, a
        mov  dptr, #0x220C      ; D0_EP_MOD = 0
        movx @dptr, a
        mov  dptr, #0x223C      ; USB_CTRL = 0: DP pull-up off
        movx @dptr, a
        clr  _IE_USB            ; IE_USB bit
        mov  r7, #0x00          ; short settle loop
    00001$:
        mov  r6, #0x00
    00002$:
        djnz r6, 00002$
        djnz r7, 00001$
        ljmp 0xF400             ; into the ISP bootloader
    __endasm;
}

/* Timer0, mode 1, 1 ms period at Fsys/12 = 2 MHz. */
void timer0_init(void)
{
    TMOD = (TMOD & 0xF0) | 0x01;
    TH0 = 0xF8;
    TL0 = 0x30;
    ET0 = 1;
    TR0 = 1;
}

void timer0_irq(void)
{
    TH0 = 0xF8;
    TL0 = 0x30;
    tick_1ms = 1;
}
