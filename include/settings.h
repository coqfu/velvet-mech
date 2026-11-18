#ifndef CH555_SETTINGS_H
#define CH555_SETTINGS_H

#include "ch555.h"

/* Settings live in the CH555's 1 KiB Data-Flash (EEPROM) at 0xF000, so the
 * keyboard keeps its look after a power cycle.  Four sections are stored
 * (settings, remap, custom picture, macros), each in its own 64-byte block
 * range and each with its own magic and checksum; writes are debounced so a
 * burst of Fn presses costs one erase. */

void settings_load(void);
void settings_task(void);       /* call once per ms; schedules the save */

/* Marks the settings dirty after a change made outside led.c. */
void settings_touch(void);
void settings_touch_map(void);
void settings_touch_custom(void);
void settings_touch_macro(void);

/* Adopts the current values without scheduling a save (used for host-driven
 * changes that are only persisted when the host asks for it). */
void settings_sync(void);

/* Write every queued section immediately and report whether the bytes read
 * back correctly.  1 means the flash now holds them; 0 means at least one
 * section failed verification and the debounced retry will try again. */
UINT8 settings_save_all_now(void);

/* Completed save rounds since boot (saturating), for the diagnostics page. */
extern UINT8 settings_writes;

/* Last ROM-controller result (0 ok, 1 bad address, 2 command error, 0xFF none
 * since reset) and whether the block verified at boot. */
extern UINT8 settings_rom_status;
extern UINT8 settings_block_valid;

/* How often the save path was entered and how often a live value differed
 * from the stored snapshot, saturated at 255. */
extern __xdata UINT8 settings_rounds;
extern __xdata UINT8 settings_shadow_hits;

/* Write shape that last produced a verified block: 0xFF unknown, 0 one window
 * per page, 1 a fresh window per byte. */
extern __xdata UINT8 settings_shape;

/* Queued sections (bit0 settings, 1 remap, 2 picture, 3 macros) and the ticks
 * left on the save debounce. */
UINT8 settings_flags(void);
UINT16 settings_save_timer(void);

/* Write the settings block immediately; 1 if it verified. */
UINT8 settings_force_save(void);

/* Flash self-test: erases/programs the unused scratch page at 0xF3C0 and
 * records what the controller and the write-enable bit did.  Destructive only
 * to that page.  Returns the record length. */
UINT8 settings_selftest(UINT8 *out);

/* Read one byte of the data flash (0xF000..0xF3FF) without modifying it. */
UINT8 settings_flash_read(UINT16 addr);

#endif
