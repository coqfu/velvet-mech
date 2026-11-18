/*
 * USB device stack for the CH555 (E8051, internal full-speed USB device).
 *
 * Derived from WCH's CH555EVT "CompositeKM" reference example (Keil C51),
 * ported to SDCC and reduced to what a keyboard needs:
 *   interface 0: boot-compatible HID keyboard, EP1 IN
 *                report protocol: 16-byte NKRO bitmap
 *                boot protocol:   8-byte 6KRO report (BIOS/KVM fallback)
 *   interface 1: raw HID (usage page 0xFF60 / usage 0x61) for OpenRGB,
 *                64-byte reports.  Built only with -DOPENRGB.
 *   interface 2: HID consumer control (media keys), 2-byte reports
 *
 * The raw HID interface has to be *interface 1*.  OpenRGB's QMK detector is
 * registered as REGISTER_DYNAMIC_HID_DETECTOR_IPU(..., vid, pid, 1, 0xFF60,
 * 0x61) with the interface number hard-coded to 1 and an exact match in
 * BasicHIDBlock::compare(), so a keyboard whose vendor collection sits on
 * interface 2 is never even opened and simply never appears in the device
 * list.  Endpoint numbers are independent of this: the raw pair stays on
 * EP3 IN / EP2 OUT and the consumer report stays on EP5 IN.
 *
 * In DETECT_MODE interface 1 instead exports an 8-byte vendor report used by
 * the matrix detector firmware.
 *
 * Endpoint budget (fixed by the CH555's buffer carve-out):
 *   EP1  IN   64B -> keyboard   (shrinks to 32B when EP5 is on)
 *   EP2  OUT  64B -> raw HID requests
 *   EP3  IN   64B -> raw HID replies
 *   EP4  OUT  32B -> unused (too short for a 64-byte report)
 *   EP5  IN   32B -> consumer control
 *   EP6  IN   32B -> unused
 * That is why the consumer interface sits on EP5 in the OpenRGB build: EP2
 * and EP3 are the only pair that can carry a 64-byte packet in both
 * directions, and EP2 is the only 64-byte OUT endpoint the core has.
 */
#include "ch555.h"
#include "usb.h"
#include "openrgb.h"

#define THIS_ENDP0_SIZE 8
#define EP1_IN_SIZE     USB_EP1_SIZE
#define EP2_IN_SIZE     USB_EP2_SIZE

#define bUEP_R_TOG     0x80
#define bUEP_T_TOG     0x40
#define UEP_R_RES_ACK  0x00
#define UEP_R_RES_NAK  0x08
#define UEP_R_RES_STALL 0x0C
#define UEP_T_RES_ACK  0x00
#define UEP_T_RES_NAK  0x02
#define UEP_T_RES_STALL 0x03

#if defined(OPENRGB) && !defined(DETECT_MODE)
#define HAVE_RAW 1
#endif

#define ITF_KBD   0
/* The raw HID interface takes slot 1 when it is built, so consumer control
 * moves to 2.  A plain keyboard has no raw interface and consumer control is
 * interface 1.  The report-descriptor request compares against these, so a
 * mismatch makes the host fetch no consumer descriptor at all.
 *
 * HAVE_RAW has to be decided before this block: when the definition sat
 * below it, the -DOPENRGB build got ITF_CON == 1 and no ITF_RAW at all, which
 * stops the file compiling and would have put consumer control on the raw
 * interface's slot if only the missing name had been patched. */
#ifdef HAVE_RAW
#define ITF_RAW   1
#define ITF_CON   2
#else
#define ITF_CON   1
#endif

/* Consumer control lives on EP5 whenever the raw HID interface claims EP2. */
#ifdef HAVE_RAW
#define CON_EP    5
#define CON_TL    D0_EP5T_L
#define CON_RES   D0_EP5RES
#define CON_ADDR  UX_EP5_ADDR
#else
#define CON_EP    2
#define CON_TL    D0_EP2T_L
#define CON_RES   D0_EP2RES
#define CON_ADDR  UX_EP2_ADDR
#endif

volatile __bit usb_ready = 0;
volatile __bit usb_suspended = 0;
volatile __bit usb_boot_proto = 0;
volatile __bit usb_reconfigured = 0;   /* host configured us again */

static volatile __xdata UINT8 Ep1Busy = 0;
static volatile __xdata UINT8 ConBusy = 0;
#ifdef HAVE_RAW
/* A reply is sitting in the EP3 buffer and the host has not polled it yet.
 * The next raw-HID command must wait: overwriting the buffer would replace a
 * reply the host is about to read, and OpenRGB then sees the answer to a
 * different request (or a command that never answers). */
static volatile __xdata UINT8 Ep3Busy = 0;
#endif
static __xdata UINT8 UsbConfig = 0;

#ifdef HAVE_RAW
/* A 64-byte host report may arrive as one packet or, because EP2 is the only
 * outbound pipe and the host stack may split it, as two 32-byte packets.
 *
 * Completed reports are posted into a small ring that the main loop drains
 * (openrgb_task), so the interrupt never runs the protocol handler: that would
 * block the LED engine for the length of a string copy or an HSV conversion.
 * One full colour update is 69 LEDs / 15 per packet = 5 packets sent
 * back-to-back with no reply in between, and sound-reactive frames repeat that
 * 45 times a second, so the ring has to hold more than one frame: sixteen
 * slots is two frames plus slack for a flash write stalling the main loop. */
#define RAW_SLOTS 16
static __xdata UINT8 raw_buf[RAW_SLOTS][USB_RAW_SIZE];
static volatile __xdata UINT8 raw_head;   /* advanced by the ISR          */
static volatile __xdata UINT8 raw_tail;   /* advanced by the main loop    */
static volatile __xdata UINT8 raw_len;    /* half-packet accumulator      */
#endif

/* ---- descriptors ---------------------------------------------------- */

static __code const UINT8 DevDesc[] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, THIS_ENDP0_SIZE,
    0x09, 0x12,             /* idVendor  0x1209 */
    0x55, 0xC5,             /* idProduct 0xC555 */
    0x00, 0x01,             /* bcdDevice 1.00 */
    0x01, 0x02, 0x00, 0x01  /* man, prod, serial, 1 config */
};

static __code const UINT8 MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 };
static __code const UINT8 MyManuInfo[] = {
    0x18, 0x03, 'v', 0, 'e', 0, 'l', 0, 'v', 0, 'e', 0, 't', 0, '.', 0,
    'm', 0, 'e', 0, 'c', 0, 'h', 0
};
static __code const UINT8 MyProdInfo[] = {
    0x18, 0x03, 'V', 0, 'e', 0, 'l', 0, 'v', 0, 'e', 0, 't', 0, ' ', 0,
    'M', 0, 'e', 0, 'c', 0, 'h', 0
};

#ifdef DETECT_MODE
/* plain boot keyboard, 8 bytes (unused by the detector) */
static __code const UINT8 KbdRepDesc[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x08, 0x81, 0x01, 0x95, 0x03, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02,
    0x95, 0x05, 0x75, 0x01, 0x91, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19,
    0x00, 0x29, 0x91, 0x81, 0x00, 0xC0
};
#else
/* NKRO: 8 modifier bits, 8 padding bits, 112 key bits (usages 0x00..0x6F) */
static __code const UINT8 KbdRepDesc[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,     /* modifiers        */
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,     /* reserved byte    */
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08,     /* LEDs (output)    */
    0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x05, 0x07,
    0x19, 0x00, 0x29, 0x6F, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x70, 0x81, 0x02,     /* 112 key bits     */
    0xC0
};
#endif

#ifdef DETECT_MODE
/* vendor defined, 8 raw bytes, used by the matrix detector */
static __code const UINT8 ConRepDesc[] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x08,
    0x09, 0x01, 0x81, 0x02, 0xC0
};
#else
/* consumer control: one 16-bit usage (volume up/down, mute, play/pause, ...) */
static __code const UINT8 ConRepDesc[] = {
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0
};
#endif

#ifdef HAVE_RAW
/* OpenRGB looks for exactly this usage page / usage pair, and expects a
 * 64-byte input and 64-byte output report. */
static __code const UINT8 RawRepDesc[] = {
    0x06, 0x60, 0xFF,       /* usage page 0xFF60                */
    0x09, 0x61,             /* usage 0x61                       */
    0xA1, 0x01,             /* collection (application)         */
    0x09, 0x62,             /*   usage 0x62                     */
    0x15, 0x00,             /*   logical min 0                  */
    0x26, 0xFF, 0x00,       /*   logical max 255                */
    0x95, 0x40,             /*   report count 64                */
    0x75, 0x08,             /*   report size 8                  */
    0x81, 0x02,             /*   input  (data, var, abs)        */
    0x09, 0x63,             /*   usage 0x63                     */
    0x95, 0x40,             /*   report count 64                */
    0x75, 0x08,             /*   report size 8                  */
    0x91, 0x02,             /*   output (data, var, abs)        */
    0xC0
};
#endif

static __code const UINT8 CfgDesc[] = {
    /* configuration */
    0x09, 0x02,
#ifdef HAVE_RAW
    0x5B, 0x00, 0x03,
#else
    0x3B, 0x00, 0x02,
#endif
    /* bmAttributes 0x80 = bus powered, no remote wakeup.  It used to say 0xA0
     * (self powered *and* remote wakeup): the board is powered from the same
     * cable on which it enumerates, and the firmware never signals a resume,
     * so both bits described something the hardware cannot do.  A host that
     * believes a device is self powered does not reserve its current from the
     * port, which is how a busy hub ends up browning out a keyboard.
     * bMaxPower 0xFA = 500 mA, the USB 2.0 maximum for a bus-powered device
     * and the honest figure for 69 LEDs; the old 0x32 (100 mA) was below the
     * board's real draw. */
    0x01, 0x00, 0x80, 0xFA,
    /* interface 0: keyboard */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    sizeof(KbdRepDesc) & 0xFF, sizeof(KbdRepDesc) >> 8,
    0x07, 0x05, 0x81, 0x03, EP1_IN_SIZE, 0x00, 0x01,
#ifdef HAVE_RAW
    /* interface 1: raw HID for OpenRGB (must be 1, see file header) */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    sizeof(RawRepDesc) & 0xFF, sizeof(RawRepDesc) >> 8,
    0x07, 0x05, 0x83, 0x03, USB_RAW_SIZE, 0x00, 0x01,
    0x07, 0x05, 0x02, 0x03, USB_RAW_SIZE, 0x00, 0x01,
#endif
    /* interface 1 on a plain keyboard, interface 2 when the raw HID took 1 */
#ifdef HAVE_RAW
    0x09, 0x04, 0x02, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
#else
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
#endif
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    sizeof(ConRepDesc) & 0xFF, sizeof(ConRepDesc) >> 8,
    0x07, 0x05, 0x80 | CON_EP, 0x03,
    /* 1 ms, like the keyboard endpoint.  This was still 10 ms, so a media
     * key (volume, play/pause) waited up to ten times longer than a normal
     * keystroke for the host to collect it. */
#ifdef HAVE_RAW
    0x02, 0x00, 0x01
#else
    EP2_IN_SIZE, 0x00, 0x01
#endif
};

/* ---- endpoint helpers ----------------------------------------------- */

__bit usb_kbd_busy(void)
{
    return Ep1Busy;
}

__bit usb_consumer_busy(void)
{
    return ConBusy;
}

void usb_kbd_send(const UINT8 *report, UINT8 len)
{
    UINT8 i;
    UINT8 ie;

    for (i = 0; i < len; i++) {
        pD0_EP0_BUF[UX_EP1_ADDR + i] = report[i];
    }
    D0_EP1T_L = len;
    Ep1Busy = 1;

    /* The USB interrupt flips the EP1 data bit and re-NAKs the endpoint when
     * the host collects a report.  Both sides read-modify-write D0_EP1RES, and
     * this sequence can be preempted in the middle of it.  If that happens the
     * ACK write (or the toggle flip) is lost: the host then sees a duplicate
     * report and drops it, and every later report carries the wrong toggle, so
     * the keyboard looks stuck or dead until it is replugged.  A few cycles
     * with interrupts off removes the race; the LED inhibition slot is
     * hundreds of microseconds wide, so nothing is disturbed. */
    ie = EA;
    EA = 0;
    D0_EP1RES = (D0_EP1RES & ~MASK_UEP_X_RES) | UEP_X_RES_ACK;
    EA = ie;
}

void usb_consumer_send(UINT16 usage)
{
    UINT8 ie;

    pD0_EP0_BUF[CON_ADDR + 0] = (UINT8)(usage & 0xFF);
    pD0_EP0_BUF[CON_ADDR + 1] = (UINT8)(usage >> 8);
    CON_TL = EP2_IN_SIZE;
    ConBusy = 1;
    /* Same race as EP1 above: the completion handler flips the toggle. */
    ie = EA;
    EA = 0;
    CON_RES = (CON_RES & ~MASK_UEP_X_RES) | UEP_X_RES_ACK;
    EA = ie;
}

#ifdef HAVE_RAW
void raw_hid_send(const UINT8 *data) __reentrant
{
    UINT8 i;
    UINT8 ie;

    for (i = 0; i < USB_RAW_SIZE; i++) {
        pD0_EP0_BUF[UX_EP3_ADDR + i] = data[i];
    }
    D0_EP3T_L = USB_RAW_SIZE;
    Ep3Busy = 1;
    /* EP3 has the same completion handler and the same lost-update hazard. */
    ie = EA;
    EA = 0;
    D0_EP3RES = (D0_EP3RES & ~MASK_UEP_X_RES) | UEP_X_RES_ACK;
    EA = ie;
}

/* True while a raw-HID reply is queued but the host has not collected it. */
__bit usb_raw_tx_busy(void)
{
    return Ep3Busy;
}
#endif

#ifdef DETECT_MODE
void usb_send_raw(const UINT8 *data)
{
    UINT8 i;
    for (i = 0; i < EP2_IN_SIZE; i++) {
        pD0_EP0_BUF[UX_EP2_ADDR + i] = data[i];
    }
    D0_EP2T_L = EP2_IN_SIZE;
    ConBusy = 1;
    D0_EP2RES = (D0_EP2RES & ~MASK_UEP_X_RES) | UEP_X_RES_ACK;
}

__bit usb_raw_busy(void)
{
    return ConBusy;
}
#endif

/* copy from __code to the xdata EP0 buffer */
static __code const UINT8 *__xdata pDescr;
static __xdata UINT8 D0SetupReqCode;
static __xdata UINT8 D0SetupLen;

static void descr_load(UINT8 len)
{
    UINT8 i;
    for (i = 0; i < len; i++) {
        pD0_EP0_BUF[i] = pDescr[i];
    }
}

/* ---- EP0 / standard request handling -------------------------------- */

static void usb_handle_setup(void)
{
    UINT8 len = 0;
    PUINT8X setup = pD0_EP0_BUF;

    D0SetupLen = setup[6];
    if (setup[7] || D0SetupLen > 0x7F) {
        D0SetupLen = 0x7F;
    }
    D0SetupReqCode = setup[1];

    if ((setup[0] & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
        /* HID class requests: GET/SET_REPORT, GET/SET_IDLE, GET/SET_PROTOCOL */
        switch (D0SetupReqCode) {
        case 0x01: /* GET_REPORT  */
        case 0x02: /* GET_IDLE    */
            break;
        case 0x03: /* GET_PROTOCOL */
            pD0_EP0_BUF[0] = usb_boot_proto ? 0 : 1;
            len = 1;
            break;
        case 0x09: /* SET_REPORT (LED state) - ignored for now */
        case 0x0A: /* SET_IDLE                   */
            break;
        case 0x0B: /* SET_PROTOCOL               */
            /* wValue 0 = boot protocol, 1 = report protocol.
             *
             * This request is per interface, but the report layout is one
             * global choice.  Acting on it for the raw HID or the consumer
             * interface would silently drop the keyboard to its six-key boot
             * report, and every key beyond the sixth would then be missing
             * until the device was replugged.  Only the keyboard interface
             * owns the setting. */
            if (setup[4] == ITF_KBD) {
                usb_boot_proto = (setup[2] == 0) ? 1 : 0;
            }
            break;
        default:
            len = 0xFF;
            break;
        }
    } else {
        switch (D0SetupReqCode) {
        case USB_GET_DESCRIPTOR:
            switch (setup[3]) {
            case 1:
                pDescr = DevDesc;
                len = sizeof(DevDesc);
                break;
            case 2:
                pDescr = CfgDesc;
                len = sizeof(CfgDesc);
                break;
            case 3:
                switch (setup[2]) {
                case 1: pDescr = MyManuInfo; len = sizeof(MyManuInfo); break;
                case 2: pDescr = MyProdInfo; len = sizeof(MyProdInfo); break;
                default:
                    pDescr = MyLangDescr;
                    len = sizeof(MyLangDescr);
                    break;
                }
                break;
            case USB_DESCR_TYP_REPORT:
                if (setup[4] == ITF_KBD) {
                    pDescr = KbdRepDesc;
                    len = sizeof(KbdRepDesc);
                } else if (setup[4] == ITF_CON) {
                    pDescr = ConRepDesc;
                    len = sizeof(ConRepDesc);
#ifdef HAVE_RAW
                } else if (setup[4] == ITF_RAW) {
                    pDescr = RawRepDesc;
                    len = sizeof(RawRepDesc);
#endif
                } else {
                    len = 0xFF;
                }
                break;
            default:
                len = 0xFF;
                break;
            }
            if (len != 0xFF) {
                if (D0SetupLen > len) {
                    D0SetupLen = len;
                }
                len = (D0SetupLen >= THIS_ENDP0_SIZE) ? THIS_ENDP0_SIZE : D0SetupLen;
                descr_load(len);
                D0SetupLen -= len;
                pDescr += len;
            }
            break;

        case USB_SET_ADDRESS:
            D0SetupLen = setup[2];
            break;

        case USB_GET_CONFIGURATION:
            pD0_EP0_BUF[0] = UsbConfig;
            if (D0SetupLen >= 1) {
                len = 1;
            }
            break;

        case USB_SET_CONFIGURATION:
            UsbConfig = setup[2];
            if (UsbConfig) {
                /* A host can reconfigure without a bus reset.  Any report that
                 * was queued then belongs to a transfer that no longer exists;
                 * leaving the busy bit set would make the keyboard silent until
                 * the next replug and the host would keep the last key down. */
                D0_EP1RES = UEP_X_RES_NAK;
                Ep1Busy = 0;
                ConBusy = 0;
#ifdef HAVE_RAW
                /* EP2 becomes the 64-byte OUT pipe, EP5 carries consumer
                 * reports, EP3 is the 64-byte raw HID reply pipe. */
                D0_EP_MOD = bUX_DEV_EN | bUX_EP2O_EN | bUX_EP5I_EN;
                D0_EP2RES = bUEP_X_AUTO_TOG | UEP_X_RES_ACK;
                D0_EP3RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
                D0_EP5RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
                Ep3Busy = 0;
                raw_len = 0;
#else
                /* No raw interface: EP2 carries the consumer reports, and it
                 * must toggle automatically like every other IN pipe here.
                 * Left in manual mode the completion handler below would have
                 * to flip the bit, which it does not, so every media report
                 * after the first looked like a retransmission to the host. */
                D0_EP2RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
                D0_EP_MOD = bUX_DEV_EN;
#endif
                usb_ready = 1;
                usb_reconfigured = 1;
                /* HID 7.2.6: a new configuration starts in report protocol.
                 * A host that wants boot reports sends SET_PROTOCOL again. */
                usb_boot_proto = 0;
            } else {
                /* SET_CONFIGURATION(0) tears the interfaces down.  Leaving
                 * usb_ready set kept the endpoints armed and let report code
                 * keep pushing on pipes the host has just disowned. */
                usb_ready = 0;
                usb_reconfigured = 1;
                D0_EP1RES = UEP_X_RES_NAK;
                D0_EP_MOD = bUX_DEV_EN;
                Ep1Busy = 0;
                ConBusy = 0;
#ifdef HAVE_RAW
                Ep3Busy = 0;
                raw_len = 0;
#endif
            }
            break;

        case USB_CLEAR_FEATURE:
            if ((setup[0] & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                switch (setup[4]) {
                /* Clearing a halt resets the data toggle to DATA0 (USB 2.0
                 * 9.4.5).  EP1 toggles by hand in the completion handler, so
                 * writing the result bits alone is right for it.  EP3 and the
                 * consumer endpoint toggle automatically, and the old code
                 * dropped bUEP_X_AUTO_TOG here: after one CLEAR_FEATURE the
                 * hardware stopped flipping the bit, every reply looked like a
                 * duplicate of the previous one and the host silently ignored
                 * all of them until the keyboard was replugged.  Re-arm the
                 * automatic toggle and drop the stale busy flag as well. */
                case 0x81:
                    D0_EP1RES = UEP_X_RES_NAK;
                    Ep1Busy = 0;
                    /* A queued report dies with the reset toggle, and the
                     * host starts from DATA0.  If that lost report was a
                     * release the host would keep the key auto-repeating, so
                     * throw away the cached copy and let the next
                     * keyboard_report() send the full current state. */
                    usb_reconfigured = 1;
                    break;
                case 0x80 | CON_EP:
                    CON_RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
                    ConBusy = 0;
                    break;
#ifdef HAVE_RAW
                case 0x83:
                    D0_EP3RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
                    Ep3Busy = 0;
                    break;
                case 0x02: D0_EP2RES = bUEP_X_AUTO_TOG | UEP_X_RES_ACK; raw_len = 0; break;
#endif
                default: len = 0xFF; break;
                }
            } else if ((setup[0] & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE) {
                /* remote wakeup: not advertised, accept silently */
            } else {
                len = 0xFF;
            }
            break;

        case USB_GET_STATUS:
            pD0_EP0_BUF[0] = 0x00;
            pD0_EP0_BUF[1] = 0x00;
            len = (D0SetupLen >= 2) ? 2 : D0SetupLen;
            break;

        case USB_GET_INTERFACE:
            pD0_EP0_BUF[0] = 0x00;
            len = (D0SetupLen >= 1) ? 1 : D0SetupLen;
            break;

        default:
            len = 0xFF;
            break;
        }
    }

    if (len == 0xFF) {
        D0SetupReqCode = 0xFF;
        D0_EP0RES = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
    } else if (len <= THIS_ENDP0_SIZE) {
        D0_EP0T_L = len;
        D0_EP0RES = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
    } else {
        D0_EP0T_L = 0;
        D0_EP0RES = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
    }
}

#ifdef HAVE_RAW
/* One OUT packet from the raw HID interface.  EP2 accepts 64 bytes in a
 * single packet, but a host stack that splits large interrupt transfers may
 * deliver the same report as two 32-byte packets, so both are handled.
 *
 * __reentrant keeps `i` off the shared overlay block: this runs from the USB
 * interrupt and would otherwise clobber the source pointer of
 * raw_hid_send(), which is live in the main loop when a reply is copied into
 * the EP3 buffer. */
static void usb_raw_out(void) __reentrant
{
    UINT8 len = D0_RX_LEN;
    UINT8 slot = raw_head;
    UINT8 i;

    if (!(D0_STATUS & bUXS_TOG_OK) || len == 0 || len > USB_RAW_SIZE) {
        raw_len = 0;
        return;
    }

    if (len == USB_RAW_SIZE) {
        raw_len = 0;
        for (i = 0; i < USB_RAW_SIZE; i++) {
            raw_buf[slot][i] = pD0_EP0_BUF[UX_EP2_ADDR + i];
        }
        if (((UINT8)(raw_head + 1) & (RAW_SLOTS - 1)) != raw_tail) {
            raw_head = (UINT8)((raw_head + 1) & (RAW_SLOTS - 1));
        }
        return;
    }

    if (len == (USB_RAW_SIZE / 2)) {
        if (raw_len == 0) {
            for (i = 0; i < len; i++) {
                raw_buf[slot][i] = pD0_EP0_BUF[UX_EP2_ADDR + i];
            }
            raw_len = len;
        } else {
            for (i = 0; i < len; i++) {
                raw_buf[slot][len + i] = pD0_EP0_BUF[UX_EP2_ADDR + i];
            }
            raw_len = 0;
            if (((UINT8)(raw_head + 1) & (RAW_SLOTS - 1)) != raw_tail) {
                raw_head = (UINT8)((raw_head + 1) & (RAW_SLOTS - 1));
            }
        }
        return;
    }

    raw_len = 0;                /* anything else is not our framing */
}

__bit usb_raw_ready(void)
{
    return (raw_tail != raw_head) ? 1 : 0;
}

const UINT8 *usb_raw_packet(void)
{
    return (const UINT8 *)&raw_buf[raw_tail][0];
}

void usb_raw_done(void)
{
    raw_tail = (UINT8)((raw_tail + 1) & (RAW_SLOTS - 1));
}
#endif

static void usb_handle_transfer(void)
{
    UINT8 status = D0_STATUS;
    UINT8 token = status & (bUXS_SETUP_ACT | MASK_UXS_TOKEN | MASK_UXS_ENDP);

    if ((token & MASK_UXS_TOKEN) == UXS_TOKEN_IN) {
        switch (token & MASK_UXS_ENDP) {
        case 1:
            D0_EP1T_L = 0;
            D0_EP1RES ^= bUEP_X_TOG;
            Ep1Busy = 0;
            D0_EP1RES = (D0_EP1RES & ~MASK_UEP_X_RES) | UEP_X_RES_NAK;
            break;
        case CON_EP:
            CON_TL = 0;
            /* No manual toggle here.  EP5 is armed with bUEP_X_AUTO_TOG, so
             * the hardware has already flipped the data toggle for the packet
             * the host just took.  Flipping it again here cancelled that
             * flip, so the next consumer report went out with the toggle the
             * host had already seen.  The host ACKs it as a retransmission
             * and throws the payload away, so a volume or play/pause key
             * appeared to repeat, or to do nothing, until the keyboard was
             * replugged.  EP3's handler below has always been correct for the
             * same reason. */
            CON_RES = (CON_RES & ~MASK_UEP_X_RES) | UEP_X_RES_NAK;
            ConBusy = 0;
            break;
#ifdef HAVE_RAW
        case 3:
            D0_EP3T_L = 0;
            D0_EP3RES = (D0_EP3RES & ~MASK_UEP_X_RES) | UEP_X_RES_NAK;
            Ep3Busy = 0;
            break;
#endif
        case 0:
            switch (D0SetupReqCode) {
            case USB_GET_DESCRIPTOR: {
                UINT8 len = (D0SetupLen >= THIS_ENDP0_SIZE) ? THIS_ENDP0_SIZE : D0SetupLen;
                descr_load(len);
                D0SetupLen -= len;
                pDescr += len;
                D0_EP0T_L = len;
                D0_EP0RES ^= bUEP_T_TOG;
                break;
            }
            case USB_SET_ADDRESS:
                D0_ADDR = D0SetupLen;
                D0_EP0RES = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            default:
                D0_EP0T_L = 0;
                D0_EP0RES = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            }
            break;
        default:
            break;
        }
    } else if ((token & MASK_UXS_TOKEN) == UXS_TOKEN_OUT) {
        UINT8 ep = token & MASK_UXS_ENDP;

        if (ep == 0) {
            D0_EP0RES ^= bUEP_R_TOG;
        }
#ifdef HAVE_RAW
        else if (ep == 2) {
            /* EP2 is ACK-by-default with automatic toggle: the hardware keeps
             * the pipe open, this handler just consumes the payload. */
            usb_raw_out();
        }
#endif
    } else if ((token & (bUXS_SETUP_ACT | MASK_UXS_TOKEN)) ==
               (bUXS_SETUP_ACT | UXS_TOKEN_FREE)) {
        D0_EP0RES = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
        usb_handle_setup();
    }
}

void usb_irq(void)
{
    UINT8 us = USB_IF;
    UINT8 awake;

    if (us & bUX_IF_D0_TRANS) {
        do {
            usb_handle_transfer();
            D0_STATUS = 0;
        } while (USB_IF & bUX_IF_D0_TRANS);
    } else if (us & bUX_IF_BUS_RST) {
        D0_EP0RES = UEP_R_RES_ACK | UEP_T_RES_NAK;
        D0_EP1RES = UEP_X_RES_NAK;
#ifdef HAVE_RAW
        D0_EP2RES = bUEP_X_AUTO_TOG | UEP_X_RES_ACK;
        D0_EP3RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
        D0_EP5RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
        D0_EP_MOD = bUX_DEV_EN | bUX_EP2O_EN | bUX_EP5I_EN;
#else
        D0_EP2RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
        D0_EP_MOD = bUX_DEV_EN;
#endif
        D0_ADDR = 0;
        HB_ADDR = 0x7F;
        USB_IF = 0xFF;
        usb_ready = 0;
        Ep1Busy = 0;
        ConBusy = 0;
#ifdef HAVE_RAW
        Ep3Busy = 0;
        raw_len = 0;
#endif
        D0SetupReqCode = 0xFF;
        USB_IE = bUX_IE_SUSPEND | bUX_IE_TRANSFER | bUX_IE_BUS_RST;
        /* Clear the reset flag by writing that one bit.  `USB_IF |= ...` is a
         * read-modify-write of a write-1-to-clear register, so it would also
         * clear a transfer completion that arrived in the meantime. */
        USB_IF = bUX_IF_BUS_RST;
    } else if (us & bUX_IF_SUSPEND) {
        /* bUX_SUSPEND (bit 3) is the current bus state; bUX_IF_SUSPEND only
         * says that state changed.  Clear just that flag: writing D0_STATUS
         * here also clears a transfer completion that arrived since the
         * snapshot, and its handler would never run, which leaves the
         * endpoint marked busy for ever.  A plain write rather than `|=`
         * because this is a write-1-to-clear register and the read half of a
         * read-modify-write would catch a flag that just arrived. */
        USB_IF = bUX_IF_SUSPEND;
        awake = (us & bUX_SUSPEND) ? 0 : 1;
        if (awake && usb_suspended) {
            /* Resume edge.  The host may have thrown away every key it
             * believed was down while the bus slept, so the next report has
             * to carry the whole state instead of a delta from a cache the
             * host no longer shares.  Without this a key that was held when
             * the machine went to sleep could stay repeating after wake. */
            usb_reconfigured = 1;
        }
        usb_suspended = awake;
    } else {
        /* Nothing this firmware acts on.  Clear exactly the flags that were
         * set (FIFO overflow, hub, sub-device 1/2) so the interrupt does not
         * immediately re-fire, but never write D0_STATUS from here: doing so
         * would silently clear a transfer completion.  A plain write, not
         * `|=`, because the register is write-1-to-clear and the read half of
         * an RMW would pick up a flag that arrived after the snapshot. */
        USB_IF = us;
    }
}

void usb_init(void)
{
    IE_USB = 0;
    USB_CTRL = 0;

    D0_EP0RES = UEP_R_RES_ACK | UEP_T_RES_NAK;
    D0_EP1RES = UEP_X_RES_NAK;
#ifdef HAVE_RAW
    D0_EP2RES = bUEP_X_AUTO_TOG | UEP_X_RES_ACK;
    D0_EP3RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
    /* EP5 toggles automatically as well, before the host has configured the
     * device and again after every configuration change, so the data toggle
     * is never flipped by hand (see usb_handle_transfer). */
    D0_EP5RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
    D0_EP_MOD = bUX_DEV_EN | bUX_EP2O_EN | bUX_EP5I_EN;
#else
    D0_EP2RES = bUEP_X_AUTO_TOG | UEP_X_RES_NAK;
    D0_EP_MOD = bUX_DEV_EN;
#endif
    D0_EP1T_L = 0;
    D0_EP2T_L = 0;
    D0_ADDR = 0;
    HB_ADDR = 0x7F;   /* route hub packets to device 0 */
    USB_IF = 0xFF;
    USB_IE = bUX_IE_SUSPEND | bUX_IE_TRANSFER | bUX_IE_BUS_RST;
    USB_CTRL = bUX_DP_PU_EN;
    IE_USB = 1;
}
