#ifndef CH555_MACRO_H
#define CH555_MACRO_H

#include "ch555.h"

/*
 * Host-recorded macros.
 *
 * The board is a plain keyboard: it cannot watch the USB bus to record a
 * sequence, so the recording is done by the KeyForge app (which sees the same
 * physical keys through the browser) and the resulting steps are uploaded
 * here.  Playback is entirely local, so a macro keeps working with no host
 * software running.
 *
 * A step is a HID usage plus how long to hold it, in 10 ms units.  Steps are
 * played back to back with a short release gap in between, so a host sees each
 * key as a separate press.  Slots are bound to a key index (0..62); 0xFF means
 * unbound.
 */

#define MACRO_SLOTS      4
#define MACRO_STEPS      24
#define MACRO_UNBOUND    0xFF
#define MACRO_GAP_MS     15

typedef struct {
    UINT8 usage;        /* HID usage, 0 is an empty step (a short pause)  */
    UINT8 hold;         /* hold time in 10 ms units, 0 means 10 ms        */
} macro_step;

extern __xdata UINT8 macro_key[MACRO_SLOTS];
extern __xdata UINT8 macro_len[MACRO_SLOTS];
extern __xdata macro_step macro_steps[MACRO_SLOTS][MACRO_STEPS];

void macro_init(void);
void macro_reset(void);

/* 1 ms tick from the main loop. */
void macro_task(void);

/* Called from the key scan when a switch closes. */
void macro_key_event(UINT8 key);

void macro_stop(void);
UINT8 macro_is_active(void);

/* 1 when this key index is bound to a non-empty macro.  keyboard_report()
 * uses it so the trigger key does not also type its own character. */
UINT8 macro_bound(UINT8 key);

/* Usage the report builder should hold down this millisecond, 0 for none. */
UINT8 macro_usage(void);

#endif
