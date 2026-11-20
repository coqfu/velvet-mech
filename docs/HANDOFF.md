# Handoff — Velvet Mech firmware

Updated 18 Nov 2026.  State after the back-light / 16-common / refresh-rate build
was flashed and verified on hardware.

## Latest changes (app + firmware surface)

* **35 effects** in `src/led.c` (`LED_FX_*` 0–34) plus test/channel-walk; every
  one is mirrored in `app/ui/src/lib/effects.ts` and drawn from the same
  arithmetic in the preview.  The wire-mode mapping lives in `EFFECT_WIRE`
  (server) and `wireMode()` (UI).
* **Sound page (host-side)**: `app/audio.py` captures the default sink's
  monitor with `parec`, analyses 14 log bands + RMS + a kick detector, renders
  one 69-LED frame and streams it over `0x09` at ~45 fps.  Seven modes, six
  palettes, gain and a brightness ceiling.  The board is switched to Direct
  while it runs and restored on stop.  Nothing is stored on the keyboard and
  no new firmware command was added.
* **KeyForge UI finished**: Lighting, Effects, Keymap, Macros, Profiles,
  Advanced, all driven by `app/server.py`.  No page offers a control without a
  firmware command behind it.
* **Keymap**: base-layer remap over `0x73`, persisted; `keymap_override[]` is
  restored from `0xF040`.
* **Macros**: 4 slots × 24 steps (`0x77`/`0x78`), recorded in the browser,
  played by `src/macro.c` from the 1 ms tick.
* **Calibration**: `led_cal[3]` white balance (`0x79`) and `led_gain[8]` row
  trim (Fn+↑/↓ in channel-walk); both persist.
* **Back light**: own level + RGB (`led_back_level`, `led_back_rgb`), still on
  COM30/31, independent of the key effects.  Some panels only render it at
  high drive.
* **Bootloader**: `CMD_BOOTLOADER (0x7A)` → `boot_enter()` → `ljmp 0xF400`;
  `POST /api/flash` enters it and runs patched wchisp on `build/keyboard.hex`.
* **OpenRGB mode numbering fixed**: OpenRGB numbers the modes it builds by
  their position in the advertised list, not by their QMK id, and assigns
  Direct last (it is created last, then shown first).  The firmware used to
  expect QMK ids, so every `SET_MODE` landed one or more places off: "Static"
  enabled Direct, "Breathing" selected Static, and Cycle All / Hue Wave /
  Solid Reactive did nothing.  `src/openrgb.c` now speaks positions
  (`Static` 1, `Breathing` 2, `Cycle All` 3, `Hue Wave` 4, `Solid Reactive` 5,
  `Direct` 6), `GET_MODE_INFO` reports them, and `EFFECT_WIRE`, `DIRECT_MODE`
  (server) and `wireMode()` (UI) follow the same numbering.
* **Typing fixed**: the debounce integrator climbed to 250 while a key was
  held and released only when it drained to zero, so every key stayed logically
  down for as long as it had been held — the next press of the same key was
  swallowed and the host kept auto-repeating after the finger lifted.
  `k_count` now saturates at `kbd_debounce`, so a release always takes the
  configured number of frames (default 2 ≈ 22 ms at 89 fps) no matter how long
  the key was down.  The keyboard endpoint's `bInterval` also went from 10 ms to
  1 ms, removing up to 10 ms of host-polling delay from every report.
* **Full-frame colour updates no longer lose the tail**: one 69-LED frame is
  five 64-byte packets (15 LEDs each) sent back-to-back with no reply, and the
  raw-HID ring held four, so the fifth was dropped and LEDs 60–68 (last three
  keys plus the whole underglow) never changed.  `RAW_SLOTS` is now 16 in
  `src/usb.c` (two frames plus slack), which also removes the back-pressure
  that sound streaming was putting on the endpoint.
* **Keymap order bug fixed**: the physical Up key sits above Down and the wide
  1.75u key is on the far right, so key index 52 is `K_UP` and 53 is `K_RSFT`.
  The enum, `row_key`, `kbd_layer0`, `host/map.json` and
  `app/ui/src/lib/keyboard-data.ts` now agree.
* **Eight more repeat/stick/lost-key causes closed** (same class as the
  integrator bug):
  1. A report that found EP1 busy was retried only on the 8 ms housekeeping
     pass.  `keyboard_report()` now clears `kbd_dirty` itself and only when
     every endpoint has taken the state, so the main loop retries every 1 ms.
  2. The report is composed **before** `led_task()` (a ~2 ms render) instead of
     after it.
  3. `rep_nk[]` only covers usages `0x00…0x6F`; a remap or macro usage in
     `0x70…0xDF` used to run past the array and overwrite the cached report,
     boot report and media report.  Out-of-range usages are now skipped (and
     shifts above `0xE7` can no longer be undefined).
  4. A boot⇄report protocol switch and a host `SET_CONFIGURATION` now
     invalidate the cached report and clear a stale endpoint-busy bit, so a key
     held across the switch is re-announced and a reconfigured keyboard cannot
     keep transmitting to a dead transfer.
  5. Arming a report is atomic against the USB interrupt.  The main loop and
     the completion handler both read-modify-write the endpoint register (ACK
     bit, data toggle).  A completion in the middle of the arm could drop the
     ACK — the endpoint then NAKed for ever — or drop the toggle flip, after
     which the host treated every report as a duplicate.  Either way the
     keyboard looked stuck or dead until replug.  `usb_kbd_send()`,
     `usb_consumer_send()` and `raw_hid_send()` now hold interrupts off for the
     last few cycles.
  6. `CLEAR_FEATURE(ENDPOINT_HALT)` re-arms `bUEP_X_AUTO_TOG` on the raw-HID
     and consumer endpoints and drops the stale busy flag.  Writing the
     handshake bits alone silently disabled automatic toggling, so every reply
     after a halt clear reused the same toggle and the host discarded it.
  7. A bus reset now also arms EP5 with automatic toggling, matching
     `SET_CONFIGURATION`.
  8. The consumer (media) endpoint still advertised `bInterval` 10 and a
     32-byte maximum packet although the firmware sends a 2-byte usage.  It is
     now 1 ms / 2 bytes, the same as the keyboard endpoint, so volume and
     playback keys no longer wait up to ten times longer than a keystroke.
* **Flash writes now wait for a gap between keystrokes** (up to 2 s) before
  starting, so the multi-millisecond write/erase stall cannot swallow the key
  that is being typed at that moment.  `keyboard_any_down()` exposes the state
  to `settings_task()`.
* **Four more found in the follow-up audit**:
  9. EP5 (consumer/media) is armed with `bUEP_X_AUTO_TOG`, but the completion
     handler also flipped the data toggle by hand.  The two cancelled out, so
     after the first media report every later one went out with a toggle the
     host had already seen: it ACKed them as retransmissions and threw the
     payload away.  A volume or play/pause key therefore repeated its first
     action, or did nothing, until replug.  EP3's handler never flipped the
     bit, which is why the raw-HID path was fine.  `usb_init()` now also arms
     EP5 with automatic toggling.
 10. The "interrupts arrive but no frame is published" fallback counted the
     deliberate pause in Direct mode.  When OpenRGB selects Direct, the
     renderer holds the current frame until the host sends its first colour;
     after 60 ms of that the fallback concluded the inhibition scan was broken
     and called `led_shutdown()`, which is permanent.  So selecting Direct and
     taking a moment over the first colour packet killed the LED engine for the
     rest of the session.  `led_paused()` now tells the two cases apart.
 11. `SET_PROTOCOL` is per interface, but the report layout is one global
     choice.  The handler acted on it for the raw-HID and consumer interfaces
     too, so a host that set boot protocol there silently dropped the keyboard
     to its six-key boot report (keys past the sixth then vanished).  Only
     interface 0 changes it now.
 12. `CLEAR_FEATURE(ENDPOINT_HALT)` on the keyboard endpoint reset the data
     toggle and killed whatever report was queued, but the cached copy still
     said it had been delivered.  If that report was a release, the next state
     compared equal and was never sent, leaving the host auto-repeating the
     key.  The clear now marks the cache stale so the full state is re-sent.

### Second audit (built, waiting for the next flash)

13. The working tree did not compile: `HAVE_RAW` was defined *after* the
    `ITF_RAW`/`ITF_CON` block in `src/usb.c`, so `-DOPENRGB` produced an
    undefined `ITF_RAW` and had already decided `ITF_CON` as 1 instead of 2.
    The definition now precedes the interface numbers.  (The detect build was
    unaffected; it never defines `HAVE_RAW`.)
14. A key with a macro bound also typed its own usage, so a macro fired *and*
    the key's own character appeared.  `keyboard_report()` now skips any key
    whose macro slot is non-empty (`macro_bound()`), including that key's media
    usage.  The suppression follows the binding, not the playback state, so the
    key stays silent after a short macro has finished while the switch is still
    held.  `keyboard_any_down()` also counts an active macro, keeping the
    debounced flash save out of the middle of playback.  Fn is refused as a
    trigger (`0x78` answers `51`, and the app hides it): it is the layer key,
    so a macro there would fire on every Fn lighting chord.
15. `0x76` replied success before touching the flash, so the app could say
    "stored" and a quick replug would lose the change.  It now calls
    `settings_save_all_now()`, which writes every queued section and only
    answers `50` after each one reads back; a failure answers `51` and paces the
    retry.  The shared save loop also calls `shadow_sync()` on success, which
    stops a duplicate write 1.5 s after every host config write.
16. Direct brightness was ambiguous.  OpenRGB's driver sends `value = 0` when it
    selects Direct, so treating the value byte as a dimmer unconditionally
    blanked the panel.  Bit 7 of the `save` byte now means "this value really is
    a dimmer": KeyForge's Levels slider sets it, OpenRGB never does.  Bit 0 is
    still the persistence flag.
17. In Direct mode the six rear LEDs kept running their own animation while the
    keys showed the host picture.  `rear_state()` now averages the host's six
    underglow colours into the one state the strip can show, at the rear-light
    level (`led_back_level`), while Direct is active and the boot animation is
    not running.  The strip keeps its own Level slider; tying it to the key
    dimmer would have made that control dead in Direct.
18. `SET_CONFIGURATION(0)` left `usb_ready` set and EP1/EP3/EP5 armed, so the
    firmware kept pushing on pipes the host had disowned.  It now clears the
    ready flag, drops the busy flags and disables the non-control endpoints; a
    new configuration also returns the device to report protocol (HID 7.2.6).
19. `led_sig()` copied the displayed frame without noticing that the LED
    interrupt had swapped buffers mid-copy, which could mix two frames in the
    diagnostic signature.  It retries (up to three times) when `led_show`
    changes during the copy.  Diagnostic-only.
20. The audio renderer folded the rear-light master level into its frame
    colours, and the firmware then applied `led_back_level` again while
    painting the strip, so a level of 64 became a quarter instead of a half.
    `app/audio.py` now sends the audio modulation only; the board owns the
    master level, exactly as it does for every other rear animation.

### App/UI audit (host-side, no flash needed)

21. Storing a macro from the Macros page always failed: the UI posts steps as
    `{usage, hold}` objects (`types.ts`), but `set_macro()` called `int(step)`
    and raised `TypeError`.  `_macro_step()` now accepts objects, old
    `[usage, hold]` pairs and bare numbers, masks both fields to 0..255, and
    rejects Fn before touching the device.  `read_macros()`/`STATE` now return
    the same object shape the UI type declares, and `fromServer()` accepts
    either form so an older service cannot turn every step into `NaN`.  The
    success toast says "written · flash save queued" because `0x78` leaves the
    section in the 1.5 s debounce rather than calling `0x76`.  Checked over
    HTTP against a stub device: dict steps round-trip, Fn answers 409 without
    sending a packet, and an empty list unbinds the slot.
22. The keymap editor offered Fn as a remap target, but `keyboard_report()`
    skips `K_FN` and reads it as the layer key, so such a write would never be
    sent.  Fn stays selectable for inspection; assignment filters it out and
    says why, and the "Changed" list no longer counts it.  The subtitle now
    says "keyboard usage", since only keyboard-page usages fit the NKRO report.
23. `val` is stored as `led_brightness = val >> 1` and read back as
    `led_brightness << 1`, so the board only holds even values 0..254.
    The brightness sliders (Lighting and Effects) now use `0..254, step 2`,
    the percentage is taken over 254, Max sends 254, and `fromServer()` snaps a
    profile saved by an older build.  Odd values used to look different on the
    slider while rendering identically.
24. Two buttons in Lighting → Keysets did nothing.  "Direct" called
    `setLighting({})`, an empty patch, so it never changed the effect; it now
    selects Direct.  "Reset brush" called `scale(brush, 1)`, an identity
    multiply; it now restores the default warm white.  Both report their
    active state.  `app/README.md` was also stale (it still claimed there were
    no debounce, remap or macro controls); its packet description and
    omissions list now match the firmware.
25. Sound-reactive playback corrupted the app's idea of the lighting state.
    `audio_start()` puts the board in Direct, then every `/api/status` poll ran
    `refresh_live_state()` and copied that temporary override into `STATE`, so
    the UI adopted Direct (with the last audio frame's colours) as the user's
    state.  An edit made then would later be applied as a Direct frame instead
    of the configured effect.  `refresh_live_state()` now leaves `STATE` alone
    while the capture thread is running; stopping restores the snapshot and
    rewrites the state from the board as before.  Verified with a stub engine:
    `read_state` is not called while `running`, and is called again afterwards.
26. Choosing a capture source while stopped looked ignored: `audio_state()`
    answered with the system default monitor, not the saved
    `AUDIO_PARAMS["source"]` that the next Start would use, so the chip never
    highlighted.  It now echoes the saved source when no engine is running
    (falling back to the monitor default), and Start still validates the name.
27. Several host writes cleared local state even when the request failed:
    "Store on keyboard" in Macros, "Write and play" (which replayed the
    start-up animation even when the config packet was rejected), the keyset
    Save button/Enter key and the profile name field all discarded the
    user's input on failure.  `saveMacro`, `applyConfig`, `saveKeyset` and
    `saveProfile` now return a success boolean; the pages only clear their
    unsaved marker, replay the preview, or empty the name field on success.
    A rejected macro store used to leave "Store on keyboard" disabled with the
    sequence still on screen; it can be retried now.
28. "Gaming mode" was described as holding a key one extra frame.  The
    firmware does no such thing: it drops usage `0xE3` (the left Windows key)
    from the report.  The Advanced hint and README now say exactly that, and
    note that the right GUI key still works.
29. A profile stored the whole `Lighting` object, including the board's remap
    and macros.  Loading one staged a keymap the Apply button cannot write, so
    the Keymap page could show a remap the board did not hold (and Apply then
    marked the draft clean).  Profiles are now saved without `remap`/`macros`
    and loading keeps the applied keymap and macros.  The TopBar profile menu
    also reset its selection after loading; leaving the name selected made
    picking the same profile again a no-op, which read as a broken "revert".
    Old profiles still load (their extra fields are ignored).
30. Small dead-code cleanup in the same pass: removed the duplicate
    `GET /api/flash` branch in the request handler, the unused
    `api.loadProfile()` client call (profiles are loaded from the list the page
    already has) and the unused `api.custom()` call.  The server routes stay
    available for scripts.  The Effects preview sentence no longer claims to
    be byte-exact with the panel: it mirrors `led.c`, but the per-channel
    gain trim (set from the keyboard's channel-walk effect) is not reported to
    the host.
31. The Effects page's group chips only changed the "x of 36" counter; the
    list itself was never filtered, so clicking "Texture" appeared to do
    nothing.  `EffectList` takes an optional `group` and the page passes its
    selection through, so the chips now really narrow the library.
32. The on-screen keyboard went black in Direct mode: `previewFrame()` only
    special-cased Custom, so effect `-1` fell through `pixelAt()`'s default
    and every key got intensity 0.  The preview now paints `colours[]` with
    the master dimmer, exactly as `render_col()`'s Direct branch does, and
    the rear strip shows the average of the six host underglow colours at the
    rear Level instead of the unrelated rear animation.
33. Direct was advertised as a colour-owning effect, which made the Effects
    page show a live base-colour picker that changed hue and saturation the
    Direct frame never uses.  Direct now reports `usesColour: false`; that
    panel instead explains that the frame is painted on the Lighting page,
    and the Lighting page labels the picker "per key" rather than "base
    colour".  Two stale docs were corrected too: the start-up animation is
    five animations plus Off (not six animations), and the OpenRGB mode
    packet is `0x07`, not `0x6`.
34. The Speed slider was disabled whenever the key effect was static, even
    though the rear light animates off the same `led_t` clock (`led_speed`
    advances it).  A static effect with a running rear animation therefore
    hid a setting that does affect the panel.  The slider is now enabled when
    either animates and the hint names which one.
35. Two copy corrections from the same pass: the Lighting page said "Painting
    switches the board to Direct", although painting only moves the workspace
    to Direct and the Apply button is what writes it; and the Keymap subtitle
    promised "any keyboard usage", while the picker is the curated list below
    and Fn is never remappable.
36. Two host-side defects with the same shape - work that looked like it had
    happened but had not.  `dirty` compared the raw speed byte, while the
    board reports it as `((led_speed - 1) << 5) | 0x10`; the firmware only
    reads bits 5-7, so the spare bit meant a speed slider moved to the value
    it already held could leave the workspace "Unapplied" after a poll.  It
    compares the 1..8 rate now.  And the Advanced page polled `/api/diag`
    once a second even with no keyboard attached - a request the service can
    only answer with `null` - so the poll now stops while the board is
    offline.
37. Last honesty/silent-write pass over the app.  The status bar and device
    chip invented `FW 0.22.0-velvet`, `PROTO 13` and `1209:C555` whenever the
    board was absent; they now show `—` and format the real ids as hex.
    Profiles that only stored an OpenRGB wire mode were labelled with
    `effectById(mode)`, which called Static "Cycle All" or "Direct"; a real
    `effectForWire()` inverse names them or prints `mode N`.  The colour
    editor printed the 0..255 hue register with a degree sign (a hue of 200
    read "200°"); it shows the 0..360 angle.  Lighting's "Painted keys" count
    included the six rear LEDs, so a Direct frame could report 69/63.  Rear
    Light wrote the whole 0x79 block on every touch, which meant clicking a
    rear colour would also commit a debounce or calibration edit still waiting
    for its own Write button; it now sends only the fields that changed.
    Copy corrected where it promised more than the hardware: rear colour is
    one colour across the strip (the "some panels render only white" hedge
    was unsupported), flashing needs the boot jumper as well as a replug, the
    Effects speed slider follows the rear clock like the Lighting one, and
    the Sound page shows `—` rates until the service answers instead of 0%.
    Bundle `index-HsS-yaii.js`; firmware unchanged.
38. `FACTORY_USAGE` in the UI had the last two keys of row 4 swapped:
    index 52 (Up) held `0xE5` (Right Shift) and index 53 (Right Shift) held
    `0x52` (Up), while `kbd_layer0[52] = 0x52` and `kbd_layer0[53] = 0xE5` in
    the firmware, and `KEYS`/`map.json` already had Up at 52.  The Keymap page
    therefore showed "R Shift" as Up's factory usage and the reverse in the
    "Changed" table; a 63-entry diff against `src/keymap.c` now matches
    exactly.  Three effect descriptions also promised motion the renderer does
    not produce: Matrix was described as "falling down" although its phase
    advances across the columns as well as the rows, and Scanner/Laser wrap
    rather than ping-pong, so they now say "drifting through" and "across the
    board".  The `LED_FX_MATRIX` comment was corrected with them.  Bundle
    `index-iErNQhj2.js`.
39. Host-side crash risk in the HID layer.  `Device` guarded reads with a plain
    `threading.Lock`, but `request()` reopened the handle *outside* that lock
    after any timeout, and `close()`/`reopen()`/`open()` took no lock at all.
    A 6 s `/api/status` poll, a profile Apply and the sound thread can all run
    at once with `ThreadingHTTPServer`, so one thread could call
    `hid_close()` while another was still blocked in `hid_read_timeout()` —
    a use-after-free inside hidapi, not a recoverable error.  The lock is now
    re-entrant and every lifecycle entry point takes it, so a close waits for
    the in-flight read; `drain()` also refuses to read a null handle.  Tested
    offline against a fake hidapi: reply pairing, timeout retry, wrong-echo
    reopen, and close-exclusion during a slow read.  Service-only change, so
    the UI bundle and the firmware hex are unchanged (`index-iErNQhj2.js`,
    md5 `392b970c…`).
40. Top key row (row 0) never followed the key effect: it stayed dark, and it
    lit up magenta when the *rear* colour was magenta.  A Direct frame with
    every key black and the rear strip magenta put magenta on Esc…Backspace,
    which proves the row is not driven from channel 1 like rows 1-4 but from
    the slots the renderer treated as back-light spares (0, 6, 7).  `poke()`
    wrote the rear colour there every frame, so with the rear light off the row
    was black and with it on the row copied the strip.  `render_col()` and
    `render_boot_col()` now mirror the row-0 colour into channels 0, 6 and 7 of
    the 14 key commons only (COM30/31 keep their six rear LEDs), and
    `spare_slot()` no longer reports those three slots as spare, so the rear
    colour can no longer overwrite the row.  Two slots are unwired, so writing
    all three is safe and does not require knowing which one is used.  The
    row's keys were never at fault: HID capture on `1209:c555` iface 0 shows
    Esc, 1, 2 and Backspace arriving correctly in 16-byte NKRO reports.
    Firmware-only change: `build/keyboard.hex` md5
    `1f4d3a861c93bb0d91310d352efed021`, image ends `0x682A` (26,666 bytes).
41. Up and Right Shift were transposed in `row_key[11..12][3]`.  Found while
    testing Fn+↑: the Up arrow produced no brightness change.  HID capture on
    `1209:c555` iface 0 shows the physical Up key alone sending a 16-byte NKRO
    report whose *only* content is modifier bit 5 (`0xE5`, Right Shift), while
    the firmware maps the cell it believes is Up to `kbd_layer0[52] = 0x52`.
    So the key the user calls Up is wired to the cell the table calls Right
    Shift.  Consequences were worse than the missing chord: the arrow typed
    Right Shift, and a Direct frame painted Up's colour onto the Shift LED
    (and the reverse) because the same table drives the LEDs.  The two cells
    are swapped, so Up is `K_UP` at col 12 / P3.4 and Right Shift is `K_RSFT`
    at col 11 / P3.3.  `host/map.json` and `app/ui/src/lib/keyboard-data.ts`
    now record the measured cells.  After the flash: Up reports `0x52`, and
    Fn+↑ steps brightness 64 → 80.  Firmware md5
    `415c1b021f0e968b10df1418def5c5d8`, `Verify OK`, first attempt; UI bundle
    `index-B6p9QOiN.js`.  Note for both this and item 40: the `Fn` brightness
    test also produced a brief blink — that is `ee_write_page()` (item 0),
    which closes `EA` for the erase and the program, so the LED ISR and USB
    stop for the duration; it is one blink 1.5 s after the last change, not a
    fault in the row.

42. Fn lighting keys made the whole panel flicker.  Measured, not guessed:
    `/api/diag` polled at ~50 Hz while the user pressed Fn+B once.  At t=11.48 s
    the press set `back` 0→1 and the save block went dirty in the next tick
    (`saveFlags 1`, `saveTimer 1493`); at t=13.36 s the save fired and the frame
    counter stopped for **0.162 s** (3 frames where ~14 were due), `writes`
    5→6.  So the flicker is the data-flash erase/program holding the CPU, which
    stops the LED scan mid-frame — one stall 1.5 s after any Fn lighting press,
    on every row at once.  The trigger is `settings_task()`'s change detector:
    it compares `led_*` against its shadow and treats a Fn change as an unsaved
    edit.  Fix: `keyboard_special()` now calls `settings_sync()` (adopt the new
    values into the shadow, write nothing) after any lighting chord, so a Fn
    press never queues a save.  Fn changes are therefore live-only and the
    stored values return after a replug; Apply in the app is the save, and its
    160 ms blink is expected.  The per-channel trim is exempt (`trimmed` flag)
    because `led_gain` has no host write path and losing the calibration on
    every replug would mean redoing it.  Firmware md5
    `fecf809e8a8479786bc625d950cceb22`, UI bundle `index-51-z0XzO.js`;
    README and the Effects page footer now state the trade-off.

43. `POST /api/save` always answered 409 "the keyboard rejected the save packet",
    while quietly writing the flash three times.  `settings_save_all_now()` writes
    four sections (settings, remap, picture, macros) before it replies, and one
    62-byte page already costs ~160 ms; the picture is 4 pages and the macro table
    5, so the reply lands about 2.4 s later.  `Device.request()` defaults to a
    400 ms read, so it timed out, called `reopen()`, retried twice — and every
    retry wrote the same bytes again (`writes` +3) before the third timeout was
    reported as a rejection.  Fix is host-side only: `save_all()` now asks for a
    5 s read.  Verified: one call, `{"ok": true}` in 2.44 s, `writes` +1,
    `blockValid` true.  The 2.4 s stop was fixed in the next build, item 44.

44. A save no longer erases pages whose bytes already match.  `ee_write_page()`
    now reads the `len` bytes it is about to write and returns early when they
    are identical to what is in the flash: the read is a plain MOVC with
    interrupts on, so it costs nothing visible, and an unchanged block keeps its
    erase budget.  "Save to flash" queues all four sections regardless of what
    changed, so this is where the win is.  Measured on the board with the app's
    save body: first save (brightness changed 95 → 127, so the settings page
    really differed) `{"ok": true}` in **0.18 s**, `writes` +1; the next two
    saves with nothing changed took **0.03 s** each, which is just the round
    trip.  Before the change the same call took 2.44 s and left the panel dark
    for all of it.  Firmware md5 `81ef8bf3e8710fbcf6ea46afe1aab47e`, flashed
    with the jumper, `Verify OK` on the first attempt.  Not yet re-tested:
    a save that follows a remap or macro edit (those pages must still differ,
    and `settings_save_all_now()` only reports success after reading each
    section back).

**Flashed and verified** on 18 Nov 2026, 18:21 UTC: `build/keyboard.hex`, md5
`392b970c14ba52f2f470a297cda03e24`, image ends at `0x6A32` (27,187 bytes;
wchisp writes 27,648 as whole 1 KiB sectors), xdata 3,342/8,192, 138 bytes of
stack free, ISR overlay clean.  The app's guided flow found the boot jumper +
power-cycle, wrote, and reported `Verify OK` on the second attempt (first one
lost the loader, as expected).  The board came back as `1209:c555` "Velvet
Mech"; `0x7E` reports layout 5, `reset: power-on`, `blockValid: true`, 89 fps,
`romStatus 0xFF` (no save attempted since boot) and the saved lighting state
read back (effect 5, brightness 127).
The user reports typing is now smooth — the integrator-lag / key-up fixes are
working on hardware.  Item-by-item checks still open (see below).

The earlier image (21504 bytes, `Verify OK`, one attempt) had already proved
the guided flash flow itself on this board.

## What is working now

Typing (NKRO), Fn layer, media keys, per-key RGB, onboard effects, EEPROM
persistence and OpenRGB per-LED control all work.

* `openrgb --list-devices` → `2: Velvet Mech` (Keyboard, protocol 13, 69 LEDs,
  zones `Keyboard` + `Underglow`, modes `[Direct] Static Breathing 'Cycle All'
  'Hue Wave' 'Solid Reactive'`, `/dev/hidraw5`).
* `tools/openrgb-test.py --info/--static/--underglow/--modes` pass; `--static`
  reports `69/69 LEDs match`.  That check walks `SET_SINGLE_LED`, which is
  answered one LED at a time, so it never exercised the five-packet bulk
  `SET_LEDS` path that was dropping its last packet.
* `tools/led-capture.py`: `fps 89`, `render 2ms`, `blackouts 0`, `stalls 0`,
  `bad dma 0`, `phase jumps 0`, `colour jumps 0`.  Frame bases `01A5` / `03A5`
  (512-byte frames, i.e. `0x1A5 + 0x200`); `tools/led-capture.py` now knows
  `FRAME_BYTES = 16 * 32`.
* Diag layout 2 (see below): `led_frames` is a `UINT16` and wraps at 65 536, so
  an apparent drop in "frames" every ~12 minutes is a wrap, not a restart.  The
  UI now labels it `Frames · 16-bit counter`.  The board also reports its reset
  cause (`PCON & MASK_RST_FLAG`), which the UI shows as *Last reset*; it reads
  `—` until the layout-2 build is flashed, because the version byte - not the
  reply length - decides whether the byte is real (HID replies are padded).

## Fixed in this session

### 0. Settings did not survive a restart — the flash writes were no-ops

Every save reported success and left the block untouched.  The erase and the
program are driven by a register triple (`ROM_ADDR_L/H`, `ROM_DAT_BUF`,
`ROM_CTRL`); on this part the triple only takes effect when all three stores
sit inline in the same function, with no helper call, status read or branch
between them.  The old code hid each operation behind `ee_erase`/`ee_program`
wrappers, so the address window had already closed before `ROM_CTRL` was
written.  Reads of the old bytes still verified, which is why `0x76` looked
like it worked.

The save path is now one inline function, `ee_write_page()` in
`src/settings.c`, and it checks its own work: `write_block()` writes the page,
verifies it with `block_ok()`, and if that fails rewrites it with a fresh
write window per byte (WCH's own driver style).  Which shape verified is kept
in `settings_shape` and reused by the remap, picture and macro writers.

Diagnostics that came out of this and are still available:

| command | payload | meaning |
|---|---|---|
| `0x6D` | — | write the settings block now; reply `50` verified / `51` failed, `rom` status |
| `0x6F` | page | read 62 bytes of data flash (`0xF000 + page*64`), read only |
| `0x7C` | — | flash write self-test: byte program, erase, both production shapes |
| `0x7E` | — | diag, layout 5: adds `settings_shape` after the save counters |

Verified after the fix: `0x6D` writes a new block (`writes` increments, bytes
change, CRC matches), the 1.5 s tick writes the same way, and `0x7A` restart
comes back with `valid=1` and the saved effect, brightness, back-light colour,
rear animation and debounce intact.  Nothing is rewritten at boot when the
block is valid — `writes=0`, `shape=0xFF` after the restart, as intended.

### 1. Keyboard was missing from OpenRGB

OpenRGB registers its QMK detector as
`REGISTER_DYNAMIC_HID_DETECTOR_IPU(name, detect, vid, pid, 1, 0xFF60, 0x61)` —
interface number hard-coded to **1** — and `BasicHIDBlock::compare()`
(`DetectionManager.cpp:111-119`) requires an exact match.  Our vendor collection
was interface 2, so the detector never opened the device.

Fix (`src/usb.c`): raw HID is interface 1, consumer control interface 2
(`ITF_RAW 1`, `ITF_CON 2`).  Endpoints unchanged (raw EP3 IN / EP2 OUT, consumer
EP5 IN); config length still `0x5B`, `bNumInterfaces` still 3.

### 2. Rainbow "not smooth" — real bug in the hue maths

`hue2rgb()` took the position inside a sector as `hue - s * 43`, but a sector is
256/6 = 42.67 wide, so at hues 128, 171 and 214 the subtraction underflowed,
wrapped to 255 and inverted the two ramps:

| hue | before (1 frame) | after |
|---|---|---|
| 128 | `(6,2,252)` deep blue | `(6,252,252)` cyan |
| 171 | `(250,6,252)` magenta | `(1,6,252)` blue |
| 214 | `(252,6,2)` red | `(252,6,249)` magenta |

Rainbow paints a whole column one colour and swept those hues ~3×/s, so an
entire column flashed to the wrong side of the wheel.  Fix: sector and position
both come from `x = hue*6; s = x >> 8; f = x & 0xFF`.  Max per-step channel
change over all 256 hues is now 6 counts (only at the 255→0 wrap).

### 3. Back light never lit — it was on two commons nothing scanned

The engine only drove COM16..COM29 (14 commons).  The datasheet maps
COM30/COM31 to **P3.6/P3.7** (SPI1 pins, unused because the firmware runs from
the internal oscillator).  That is where `led6`..`led11` sit, so no frame ever
reached their PWM registers — no colour, no white, nothing.

Also, the parts are **not colour-controlled**: they only light when the slot is
driven hard with all three colour lines on.  Half-intensity coloured values
(what the effects and OpenRGB used to send) leave them dark.

Fix (`src/led.c`):
* `LED_COLS` 14 → 16 (COM16..COM31), `LED_FB` 448 → 512.
* ISR wraps at `0x1F`; matrix sampling / `led_common_low()` skipped for
  `idx >= MATRIX_COLS` (no rows there, and forcing the pin low would fight the
  engine's own common drive).
* Every spare slot is poked `(127, 255, 255, 255)` — full intensity, white —
  whenever `led_back` is on, in every effect and during the boot sweep.  Later
  builds give the strip its own level/colour/animation and, in Direct, the
  averaged host colour (item 17).  The `ug_col[]`/`ug_ch[]` tables are gone;
  `led_sig()` still reports the 14 matrix columns so the 60-byte packet format
  is unchanged.
* Intensity PWM is now 6-bit (`LED_CYCLE = bLED_INTEN_CYC1`, 64 clocks).  The
  inhibition timer counts in intensity cycles, so the common slot drops from
  1365 µs to ~700 µs: 16 commons in 11.2 ms ≈ **89 Hz** instead of 52 Hz.
  `poke()` shifts the 0..127 intensity range down to the 6 bits the record
  carries, so duty cycle (and brightness) is unchanged.

### 4. Speeds 1-3 juddered, wave crest popped

* Hue is now taken in **quarter units** (`hue2rgb(UINT16 hue_q2)`,
  `x = hue_q2 + (hue_q2 >> 1)`).  Rainbow calls
  `hue2rgb((col * 72 + t) & 1023)`, aurora `hue2rgb((128 + row*22) << 2)`.
  At speed 4 (`t` always a multiple of 4) the output is bit-identical to the
  old build — verified in simulation over all 256 hues.
* Wave works in quarter units too and the crest fades back up over the last
  quarter of the cycle instead of jumping 40 → 255 when `d` wraps.  Max step
  across the wrap is now 2 counts (was 215).

## Windows and the single-file build (item 45)

The host app now runs on Windows as one `KeyForge.exe`; the firmware is
unchanged (same `build/keyboard.hex`).

* **Platform layer in `app/server.py`**: `_hidapi()` tries `hidapi.dll` /
  `libhidapi-0.dll` / `libhidapi.dll` on Windows (and dylib paths on macOS),
  searching `_MEIPASS`, the executable's directory and the app directory
  instead of relying on the loader's search path. If it still fails, `LIB` is
  `None`, `LIB_ERROR` holds the reason, and `main()` reports it in a message box
  and `%LOCALAPPDATA%\KeyForge\keyforge.log` rather than dying in an import.
* **State location**: `_data_dir()` keeps `app/profiles.json` for a writable
  source checkout, and uses `%LOCALAPPDATA%\KeyForge` (frozen or read-only)
  or `$XDG_DATA_HOME/keyforge`. `load_store`/`write_store` now open UTF-8
  explicitly, which matters on Windows where the default is cp1252.
* **Flashing**: `_wchisp_path()` checks `PATH`, `~/.cargo/bin/wchisp.exe`, the
  exe's directory and the in-tree build. `isp_present()` keeps the `/sys` scan
  on Linux; on Windows it runs `wchisp probe` and caches the answer 0.6 s,
  because the job polls every 200 ms and each probe is a process launch. The
  failure messages append the WinUSB/Zadig hint on Windows.
* **No console flashing**: every helper process passes `creationflags=
  NO_WINDOW` (0x08000000) on Windows.
* **Audio**: `app/audio.py` now has a `Capture` interface with `ParecCapture`
  (Linux, unchanged) and `SoundcardCapture` (WASAPI loopback via SoundCard,
  used on Windows/macOS). `KEYFORGE_CAPTURE=soundcard` forces the second one on
  Linux, which is how it was tested here: 44.8 fps sustained, 112 frames in
  2.5 s, clean stop. Two bugs surfaced in that test - SoundCard 0.4.x
  `recorder()` has no `data_type` argument (falls back to float32), and closing
  the recorder before joining the pump thread aborted libpulse
  (`pa_stream_get_state` assertion), so `close()` now joins first.
* **Packaging**: `packaging/windows/keyforge.spec` (one file, windowed,
  `hidapi.dll` from `binaries`) and `packaging/windows/build.ps1`, which installs
  `app/requirements.txt` + PyInstaller, fetches `hidapi-0.16.0` and
  `wchisp-0.3.0-win-x64`, then produces three things in `dist/windows/`:
  `KeyForge.exe`, `KeyForge-Setup-<v>.exe` (Inno Setup, per-user, no admin) and
  `KeyForge-<v>-portable.zip`. `wchisp.exe` is both inside `KeyForge.exe` and
  next to it in the install directory (`_wchisp_path` looks in `sys._MEIPASS`
  first), so flashing on a clean machine needs no Rust; only the WinUSB driver
  is left.
  `.github/workflows/windows-exe.yml` builds on `windows-latest`, uploads the
  artifact, then verifies the bundle: `--selftest` on the bare exe, a silent
  install, `--selftest` again from the installed copy, the UI fetched over HTTP,
  and a silent uninstall that must leave no exe behind. Tags attach all three
  files to the release. PyInstaller cannot cross-compile, so the binaries must
  come from a Windows runner.
* **Frozen entry point** is `packaging/windows/launcher.py`, not `server.py`: it
  writes `boot.log` before importing `server` and `crash.log` (traceback) if the
  import or `main()` raises. That exists because a windowed PyInstaller build
  turns an unhandled exception into a modal dialog, which on a headless runner
  looks like a hang. `audio.py`'s `_root()` resolves the bundle via
  `sys._MEIPASS` and reads `host/map.json` lazily for the same family of reason.
* **`--selftest`** (`server.py`) checks hidapi, the bundled UI, `host/map.json`,
  `build/keyboard.hex`, the writable data dir, `wchisp` and whether the board is
  present; hidapi/UI/map/data-dir are fatal, the rest are informational. In a
  windowed build the result is only visible in `keyforge.log`, which is why CI
  reads the log as well as the exit code.
* **Docs**: `docs/WINDOWS.md` states what is verified (the exe builds, starts
  and serves the UI in CI) and what is not (USB against the board on Windows,
  flashing on Windows, audio on Windows hardware), plus Zadig, SmartScreen and
  uninstall.

## Things to watch / next candidates

1. **Animation is ~1.7× faster in wall-clock** than before, because the clock
   advances per published frame and the frame rate went 52 → 89 Hz.  Rainbow at
   speed 4 is now ~2.9 s per revolution (was ~5.1 s).  Fn+← twice restores the
   old feel; if a slower default is wanted, change the default `led_speed`
   (flash keeps the saved value, so it only affects a factory-reset board) or
   scale the `led_t` advance.
2. **Back light ignores the dimmer** — it is binary on/off via Fn+B, because a
   reduced intensity is exactly what left it dark.  If a softer look is wanted,
   the next step is to test whether, say, 6-bit intensity 48 still lights it.
3. Effects to re-check by eye at the new refresh rate: rainbow, wave, aurora,
   breathing, and the boot sweep.
4. `tools/led-capture.py` still flags `COLOUR JUMP` on any channel move >24
   counts; samples are ~0.25 s apart and a healthy rainbow legitimately moves
   ~47 counts in that time (more now).  Compare against the clock-implied
   advance instead of a fixed threshold.

## Invariants

* CH555, 60 KiB code flash, 63 keys, 64% layout, no encoder, per-key RGB.
* Name **"Velvet Mech"**; 69 OpenRGB LEDs (63 keys + 6 underglow).
* Fn+`\` cycles effects, Fn+↑/↓ brightness (to zero), Fn+←/→ speed, Fn+B back
  light.  Back light parts: `led10 led9 led6 led11 led7 led8`.
* ISR-reachable code must not use SDCC's overlay block — `make keyboard` runs
  `tools/check-isr-overlay.py`; it must stay clean.
* Renderer must never index `led_buf[led_work]` at run time (pulls in
  `__mulint`); `frame` is one pointer set per frame.
* Settings: data flash `0xF000`, 64-byte block, `EE_VER 3`, magic + version +
  checksum.  Host-driven brightness/speed are not written unless OpenRGB sends
  the `save` byte; Fn controls never auto-save.
* Diagnostics over raw HID: `0x7E` diag, `0x7F` dump one common, `0x7D`
  60-byte frame signature.  Keep host polling under ~40 req/s.
* udev: `/etc/udev/rules.d/99-ch555-kbd.rules` (1209:c555 → 0666) and
  `99-wchisp.rules` (4348:55e0).
* **Flashing is power-on only.**  `GLOBAL_CFG.bBOOT_LOAD` is read-only and is
  cleared by any software reset, so `ljmp 0xF400` (the firmware's `0x7A`
  handler) re-enters the bootloader with the bit clear and it software-resets
  straight back into the application.  Measured: the board re-enumerates as
  `1209:c555`, `4348:55e0` never appears.  Do not describe software entry as
  working.  The app's guided flash polls `/sys/bus/usb` for `4348:55e0`, so the
  user only has to replug; `tools/flash-on-replug.sh` does the same from a
  shell.
* Work only inside `~/Documents/ch55-keyboard/`.

### Useful paths

| path | what |
|---|---|
| `src/led.c` | DMA pump, inhibition ISR, effects, `hue2rgb`, `poke`, `led_diag/sig` |
| `src/openrgb.c` | protocol D handler, zones, direct mode, mode/speed/brightness |
| `src/usb.c` | descriptors + endpoint plumbing (interface 1 = raw HID) |
| `src/settings.c` | flash block, retries, `settings_task` |
| `src/keymap.c` | matrix column/row -> HID usage tables |
| `tools/led-capture.py` | live signature/counter capture |
| `tools/openrgb-test.py` | hidapi harness `--info/--static/--modes/--underglow` |
| WCH CH555 datasheet | download from wch-ic.com; LED_CYCLE §18, common/pin map §5 (not committed: WCH copyright) |

## Verification state

Done on hardware (18 Nov, after the flash):

1. **No blackout**, `bad dma 0`, `stalls 0`, `slow renders 0`; capture shows
   88–89 fps sustained with only the dump's own phase/colour noise.
2. All **35 effect ids** accepted by `/api/apply` and read back unchanged;
   the frame signature advances for each one.
3. **Back light** level, colour and on/off write and read back (`0x79`).
4. **Debounce / gaming / calibration**: written and read back at 1..6 and
   0..255; the calibration path clamps at 255 per channel.
5. **Remap** (`0x73`) and **macro** (`0x77`/`0x78`) round-trip; `0x76` save
   returns success.
6. **Flashing** through the app: bootloader detected, `21504 bytes written`,
   `Verify OK`, one attempt, board back as `1209:c555`.
7. **Software bootloader entry is impossible** (see above): the jump runs, the
   bootloader discards it, the board restarts as a keyboard.
8. **Persistence**: `0x6D` and the debounced save both change the block and
   verify; `0x7A` restart keeps the effect, brightness, rear colour/mode,
   boot animation, debounce and calibration.  Block after a save:
   `56 4D 05 03 …` with a matching CRC; `writes` climbs by one per save.
8b. **Save across a replug, current build**: with `81ef8bf3…` on the board,
   `POST /api/save` (effect 1, val 254, speed 112, rear on) returned
   `{"ok": true}`, then the board was unplugged and plugged back: `0x7E` read
   effect 1, brightness 127, speed 4, rear light on, `writes` 0 (counts saves
   since boot), `reset: power-on`, `blockValid true`.  That also proves the
   page-skip compare writes a page that really differs, because the settings
   block was the one page that changed.
9. **Sound**: `POST /api/audio {"enabled":true}` switches the board to Direct,
   streams ~45 fps (`/api/audio` reports the measured rate), moves all 14 bands
   with real playback, and `{"enabled":false}` restores the previous effect
   (`directActive` returns to false in `0x7E`).  Applying lighting while it
   runs stops it first.

Still needs a human (the board runs `81ef8bf3e8710fbcf6ea46afe1aab47e`):

* **Media keys must keep working**: Fn+W / Fn+E volume down/up several times in
  a row, then Fn+Y play/pause twice.  Before item 9 only the first press in a
  session reached the host.
* **Direct mode must not kill the lights**: in OpenRGB select Direct and wait a
  second before painting, then paint.  `0x7E` should still show a rising frame
  counter; before item 10 the engine switched itself off after 60 ms.
* **Macro trigger (item 14)**: bind a macro to a letter, press that key once
  and confirm *only* the macro's steps appear — not the letter as well, and not
  the letter again when the macro ends while the key is held.  Then clear the
  slot and confirm the key types normally.
* **Direct underglow (item 17)**: in OpenRGB's per-LED editor set the six
  underglow LEDs to a bright colour; all six rear LEDs should follow as one
  average colour.  Move the app's rear Level slider while still in Direct and
  confirm it still changes the strip; Fn+B must still switch the strip off.
* **Save means saved (item 15)**: the call itself is fixed (item 43) and a
  changed block is still written (item 44 measured 0.18 s), but the real proof
  is one replug: press Save, unplug, plug back in, and check the effect,
  brightness, remap, Custom picture and macros are all back.  A save while
  Direct is selected must report success without pretending the Direct frame
  is stored — it stays on the host.  Also unproven: a save that follows a
  remap or macro edit, i.e. that those pages really differ and are written.
* **OpenRGB mode walk** after the same flash: `-m Static`, `-m Breathing`,
  `-m Cycle All`, `-m Hue Wave`, `-m Solid Reactive` should each select the
  named effect, and `-m Direct -c RRGGBB` should repaint all 69 LEDs including
  the underglow.
* **Typing feel**: hold a key for a second and release — it must stop within
  two matrix frames, not keep repeating.  Type a double letter quickly ("ll",
  "oo") and check neither press is swallowed.  Press Fn+↑ twice and immediately
  type: since item 42 that chord writes nothing, so there must be no blink and
  no dropped key.  `debounce` 1 in Advanced gives an ~11 ms filter; 2 (default)
  ~22 ms.
* **Eyes on the panel** for back-light colour at low levels and for effect
  smoothness at each speed step.
