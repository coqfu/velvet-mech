#ifndef CH555_OPENRGB_H
#define CH555_OPENRGB_H

#include "ch555.h"

/*
 * OpenRGB "QMK keyboard protocol", revision D, over raw HID.
 *
 * The host talks to the raw HID interface (usage page 0xFF60, usage 0x61)
 * with 64-byte reports.  Payload byte 0 of a request is the command, byte 0
 * of a reply echoes it, byte 1 is the first data byte and byte 63 is
 * OPENRGB_END_OF_MESSAGE.
 *
 * The device exposes every LED the panel can light:
 *
 *   index 0 .. 62   the 63 key backlights          (flag 0x04, keylight)
 *   index 63 .. 68  the six rear / side LEDs       (flag 0x02, underglow)
 *
 * OpenRGB numbers LEDs by their position in the GET_LED_INFO reply, so these
 * indices are also the ids used by SET_SINGLE_LED / SET_LEDS.
 */

#define OPENRGB_KEY_COUNT         63       /* key backlights            */
#define OPENRGB_UNDERGLOW_COUNT   6        /* rear / side strip          */
#define OPENRGB_LED_COUNT         (OPENRGB_KEY_COUNT + OPENRGB_UNDERGLOW_COUNT)

#define OPENRGB_PACKET_SIZE       64       /* payload bytes per report  */
#define OPENRGB_PROTOCOL_VERSION  0x0D     /* revision D, recognised by OpenRGB */

/* LED flags, as understood by OpenRGB (QMK rgb_matrix led_flags_t). */
#define OPENRGB_FLAG_UNDERGLOW    0x02
#define OPENRGB_FLAG_KEYLIGHT     0x04

/* Set while the host owns the colours ("Direct" mode). */
extern __xdata UINT8 openrgb_direct_active;

/* Cleared when Direct mode is entered and set by the first colour packet.
 * Until it is set the renderer keeps the frame that is already on screen,
 * instead of painting the still-empty colour array -- which blanked the whole
 * board for as long as the host took to send its first SET_LEDS. */
extern __xdata UINT8 openrgb_have_colors;

/* One colour per LED, indexed like keymap_rowkey() for the keys and then the
 * underglow strip; read by the LED renderer while openrgb_direct_active. */
extern __xdata UINT8 openrgb_colors[OPENRGB_LED_COUNT][3];

/* Private commands, outside the OpenRGB range, used by tools/led-capture.py.
 * They never touch the LED state, so a host that polls them cannot change what
 * the board is doing.
 *
 *   0x7E  diag   -> 1 + N bytes (N = 19) of engine state, see led_diag()
 *   0x7F  dump   -> 1 + 32 bytes  one common of the displayed frame; request
 *                   byte 1 is the common number
 *   0x7D  sig    -> 1 + 32 bytes  whole-frame snapshot, see led_sig()
 */
#define OPENRGB_CMD_SIG           0x7D
#define OPENRGB_CMD_DIAG          0x7E
#define OPENRGB_CMD_DUMP          0x7F

/* One complete 64-byte request from the host. */
void openrgb_receive(UINT8 *data) __reentrant;

/* Drains packets posted by the USB interrupt.  Call from the main loop. */
void openrgb_task(void);

/* Called when a keyboard control takes the lighting back. */
void openrgb_leave_direct(void);

#endif
