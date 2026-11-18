#ifndef CH555_BOARD_H
#define CH555_BOARD_H

#include "ch555.h"

void clock_init(void);
void delay_ms(UINT16 ms);
void timer0_init(void);
void timer0_irq(void);
extern volatile __bit tick_1ms;

/* Stop everything and branch into the WCH ISP bootloader at 0xF400 so the
 * host can flash a new image.  Does not return. */
void boot_enter(void);

/* Quiesce the USB/LED hardware and jump into the ISP bootloader at 0xF400.
 * Never returns; the bootloader re-enumerates the device. */
void boot_enter(void);

#endif
