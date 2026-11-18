#ifndef CH555_KEYBOARD_H
#define CH555_KEYBOARD_H

#include "ch555.h"

void keyboard_init(void);

/* One matrix column, sampled from the LED inhibition ISR: p4 is the raw P4
 * byte read with the five row pins (bits 1..5) pulled up.
 *
 * __reentrant is mandatory here, not a style choice: SDCC places the
 * parameters of ordinary functions in the shared overlay block, and
 * keyboard_isr_col() runs from an interrupt while the main loop may be in the
 * middle of any other function that keeps state there.  Writing that block
 * from interrupt context used to corrupt the caller (OpenRGB replies came
 * back with a mangled tail, data-flash writes were lost, effects tore). */
void keyboard_isr_col(UINT8 col, UINT8 p4) __reentrant;

/* Called by the LED ISR when a complete set of columns has been sampled; makes
 * the frame visible to keyboard_process() atomically. */
void keyboard_frame_end(void);

/* Sequence number of the most recently published matrix frame.  The main loop
 * remembers the last number it decoded, so every frame advances the debounce
 * exactly once even if the LED ISR publishes a new one during the decode. */
extern volatile __xdata UINT8 frame_seq;

/* Decode one complete frame and run the debounce.  Returns the sequence number
 * of the frame that was actually copied (the seqlock may have picked up a
 * newer frame while copying). */
UINT8 keyboard_process(void);

/* Fallback path when the LED engine is not running: drives every column in
 * turn and debounces in software.  Not used while the LED engine owns P4. */
void keyboard_scan_gpio(void);

/* Set by the scan whenever a key changes state, cleared by the report code. */
extern volatile __bit kbd_dirty;

void keyboard_report(void);
void keyboard_special(void);

/* Non-zero while at least one key is logically down.  Lets the settings code
 * wait for a gap between keystrokes before it starts a flash write. */
UINT8 keyboard_any_down(void);

/* Input behaviour the host can change.  Debounce is the number of consecutive
 * matrix frames a switch must agree on (1 = fastest, 6 = ~67 ms).  Gaming mode
 * masks the left Windows key so a slip does not leave the game. */
extern UINT8 kbd_debounce;
extern UINT8 kbd_gaming;

#endif
