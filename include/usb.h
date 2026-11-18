#ifndef CH555_USB_H
#define CH555_USB_H

#include "ch555.h"

#ifdef DETECT_MODE
#define USB_EP1_SIZE 8   /* keyboard report (unused by the detector) */
#define USB_EP2_SIZE 8   /* vendor report carrying matrix coordinates */
#else
#define USB_EP1_SIZE 16  /* NKRO bitmap: modifiers + 1 pad + 112 key bits */
#define USB_EP2_SIZE 2   /* consumer report: one 16-bit usage */
#endif

extern volatile __bit usb_ready;     /* set by SET_CONFIGURATION */
extern volatile __bit usb_suspended;
extern volatile __bit usb_boot_proto;/* 1 = boot protocol (6KRO answers) */
extern volatile __bit usb_reconfigured;/* host configured the device again */

void usb_init(void);
void usb_irq(void);

/* len is the number of bytes actually queued (8 for boot protocol, 16 for
 * the NKRO bitmap). */
void usb_kbd_send(const UINT8 *report, UINT8 len);
void usb_consumer_send(UINT16 usage);
__bit usb_kbd_busy(void);
__bit usb_consumer_busy(void);

#if defined(OPENRGB) && !defined(DETECT_MODE)
/* Raw HID interface used by OpenRGB: 64-byte reports in both directions. */
#define USB_RAW_SIZE 64
/* __reentrant: the source pointer must live on the stack, never in SDCC's
 * shared overlay block, because an LED-frame interrupt can fire during the
 * copy in the main loop.  The overlay block is one global scratch area and a
 * clobbered pointer truncates the reply with zeroes from whatever the pointer
 * ends up pointing at. */
void raw_hid_send(const UINT8 *data) __reentrant;
/* True while a reply is queued and the host has not collected it.  The command
 * handler must wait instead of overwriting it. */
__bit usb_raw_tx_busy(void);
__bit usb_raw_ready(void);
const UINT8 *usb_raw_packet(void);
void usb_raw_done(void);
#endif

#ifdef DETECT_MODE
void usb_send_raw(const UINT8 *data);
__bit usb_raw_busy(void);
#endif

#endif
