#include "ch555.h"
#include "macro.h"
#include "keyboard.h"
#include "keymap.h"

/*
 * Macro playback.
 *
 * The recorder lives in the host app; this module only stores a step list and
 * replays it.  Playback is a two-phase state machine per millisecond:
 *
 *   hold phase   m_usage is down, m_timer counts the hold time
 *   gap phase    m_usage is up,   m_timer counts the release gap
 *
 * The gap matters: if two steps of the same key were sent back to back the
 * host would never see a key-up between them and would treat them as one long
 * press.  15 ms is longer than the gap between two 1 ms report retries, and
 * the release only has to be visible to the host between two consecutive
 * steps, so it always reaches the host.
 *
 * Everything here runs from the main loop (the 1 ms tick and the report
 * builder), never from an interrupt, so no re-entrancy protection is needed.
 * kbd_dirty is only ever set here; keyboard_report() clears it once every
 * endpoint has actually taken the current state.
 */

__xdata UINT8 macro_key[MACRO_SLOTS];
__xdata UINT8 macro_len[MACRO_SLOTS];
__xdata macro_step macro_steps[MACRO_SLOTS][MACRO_STEPS];

static __xdata UINT8 m_slot;
static __xdata UINT8 m_step;
static __xdata UINT16 m_timer;      /* milliseconds left in the phase */
static __xdata UINT8 m_usage;       /* usage currently held           */
static __bit m_active;
static __bit m_gap;

void macro_reset(void)
{
    UINT8 i, j;

    for (i = 0; i < MACRO_SLOTS; i++) {
        macro_key[i] = MACRO_UNBOUND;
        macro_len[i] = 0;
        for (j = 0; j < MACRO_STEPS; j++) {
            macro_steps[i][j].usage = 0;
            macro_steps[i][j].hold = 0;
        }
    }
    m_active = 0;
    m_gap = 0;
    m_usage = 0;
    m_timer = 0;
}

void macro_init(void)
{
    macro_reset();
}

void macro_stop(void)
{
    if (m_active) {
        m_active = 0;
        m_gap = 0;
        m_usage = 0;
        m_timer = 0;
        kbd_dirty = 1;
    }
}

UINT8 macro_is_active(void)
{
    return m_active ? 1 : 0;
}

UINT8 macro_usage(void)
{
    return m_active ? m_usage : 0;
}

UINT8 macro_bound(UINT8 key)
{
    UINT8 i;

    for (i = 0; i < MACRO_SLOTS; i++) {
        if (macro_key[i] == key && macro_len[i]) {
            return 1;
        }
    }
    return 0;
}

void macro_key_event(UINT8 key)
{
    UINT8 i;

    /* Fn is a layer key, not a trigger.  The host refuses to bind it, but a
     * slot stored by an older build could still name it; firing on every Fn
     * lighting chord would be the worst possible surprise. */
    if (m_active || key >= K_COUNT || key == K_FN) {
        return;                     /* one macro at a time, no re-trigger */
    }
    for (i = 0; i < MACRO_SLOTS; i++) {
        if (macro_key[i] == key && macro_len[i]) {
            m_slot = i;
            m_step = 0;
            m_usage = 0;
            m_timer = 0;
            m_gap = 1;              /* go straight to the first step */
            m_active = 1;
            return;
        }
    }
}

void macro_task(void)
{
    UINT8 u;
    macro_step *step;

    if (!m_active) {
        return;
    }
    if (m_timer) {
        m_timer--;
        return;
    }

    if (m_gap) {
        /* The release gap is over: start the next step. */
        if (m_step >= macro_len[m_slot]) {
            macro_stop();
            return;
        }
        step = &macro_steps[m_slot][m_step];
        m_step++;
        u = step->usage;
        if (!u) {
            m_timer = 5;            /* empty step: a short pause */
            m_gap = 1;
            return;
        }
        m_usage = u;
        m_gap = 0;
        m_timer = (UINT16)step->hold * 10u;
        if (!m_timer) {
            m_timer = 10;
        }
        kbd_dirty = 1;
        return;
    }

    /* Hold is over: release the key and wait before the next one. */
    m_usage = 0;
    m_gap = 1;
    m_timer = MACRO_GAP_MS;
    kbd_dirty = 1;
}
