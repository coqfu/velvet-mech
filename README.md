# Velvet Mech — CH555 64% keyboard firmware

Replacement firmware for a CH555-based 64% keyboard whose original firmware was
lost. The matrix wiring was recovered empirically; everything below is verified
on the real device.

**Hardware: iGear Raptor only.** This firmware is written for one board, the
iGear Raptor 63-key 64% keyboard built on the WCH CH555. The matrices, the LED
commons and channels, the rear-light exceptions and the boot jumper below were
measured on that unit, so it will not run on a different keyboard without
re-deriving them (see "Porting" in `docs/INSTALL.md`).

**Start here:** `docs/INSTALL.md` walks from dependencies to a flashed keyboard.
The control app is KeyForge (`app/`), a local web UI plus a Python service.

**Windows:** KeyForge ships as an installer that bundles everything (Python,
numpy, SoundCard, hidapi, `wchisp`) - the target machine needs nothing
installed:
[download KeyForge-Setup-1.0.0.exe](https://github.com/coqfu/velvet-mech/releases).
There is also a portable zip and a bare `KeyForge.exe`; `packaging\windows\build.ps1`
builds all three. See `docs/WINDOWS.md`, including what is and is not verified
on Windows.

**Licence:** MIT for everything in this repository (`LICENSE`). The optional
flasher, [wchisp](https://github.com/ch32-rs/wchisp), is a separate GPL-2.0
program that this repository calls but does not contain.

## Status

| feature | state |
|---|---|
| USB HID keyboard | working (`1209:c555`, "Velvet Mech", boot-compatible) |
| matrix scan + debounce | working, all 63 keys verified |
| NKRO (16-byte bitmap) | working, 10-key rollover tested |
| 6KRO boot-protocol fallback | implemented (honours `SET_PROTOCOL`) |
| anti-ghosting | diodes on every key -> full N-key rollover, no phantom keys |
| Fn layer (F-keys, nav) | working |
| media keys (consumer HID) | working |
| per-key RGB | working, CH555 RGB LED engine, ~89 Hz frame / 8-bit colour |
| onboard effects | 35 firmware effects + Direct + test / channel walk |
| start-up animation | five selectable animations (sweep, rainbow, fade, scan, pulse) plus Off, at three paces, stored in data flash |
| back light | `led6`–`led11` on COM30/31, own level, colour and animation, independent of the key effects |
| rear animation | nine modes: solid, breathe, heartbeat, pulse, rainbow, reactive, strobe, sparkle, wave |
| key remap | host-written per-key HID usage, stored in data flash |
| macros | host-recorded, 4 slots × 24 steps, played by the board |
| colour calibration | per-channel white balance and per-row gain, stored in data flash |
| bootloader | power-on only (`bBOOT_LOAD` is read-only); app waits for the power cycle, then flashes |
| host control / OpenRGB | working — QMK protocol D, 63 keys + 6-LED underglow zone, Direct and five onboard effects |
| KeyForge app | React + TS control surface (`app/ui`), hardware service in `app/server.py` |

## Matrix (measured)

- **Columns** (driven low, one at a time): `P0.0–P0.7` then `P3.0–P3.5` (14)
- **Rows** (read from `P4`): `P4.1–P4.5` (5)
- Diodes conduct **column → row**. Driving a row and reading a column finds
  nothing, which is what makes the direction unambiguous.

Full key → (col, row) table: `host/map.json`.

## RGB LED wiring

The key LEDs are wired like the matrix: the common (column) line feeds the LED,
the channel (row) line sinks it.

| function | pins |
|---|---|
| commons / columns | `P0.0–P0.7` = COM16–23, `P3.0–P3.5` = COM24–29 |
| red channels | `P4.1–P4.5` (same pins as the matrix rows) |
| green channels | `P2.1–P2.5` |
| blue channels | `P1.1–P1.5` |

So the LED of the key at (col, row) is `led_frame[col*8 + (row+1)]`:
one 32-byte record per common, `[intensity, R, G, B]` x 8 channels,
16 commons x 32 bytes = 512 bytes of xRAM per frame.

**Top-row exception.** On this unit the 14 LEDs of the top key row (Esc →
Backspace) are not on channel 1 with the other four rows; they hang off the
channels nothing else uses (0, 6 and 7). The engine used to treat those slots as
back-light spares, so the row showed the rear-light colour and never followed
the key effect. `render_col()` now mirrors row 0 into all three slots (only the
wired one is visible), and `spare_slot()` no longer hands them to the rear
light. The rear LEDs themselves are unaffected: they live on COM30/31, whose
records are not mirrored.

While the LED engine runs, `P0`/`P3`/`P4` belong to it, so the matrix is sampled
one column per LED inhibition slot inside the LED ISR (exactly the scheme WCH's
own `RGBKeyboard` demo uses). One frame is 16 slots x ~0.7 ms = **11.2 ms**, so
the panel refreshes at ~89 Hz and the matrix is scanned ~89 times/s. The
debounce is an integrator on that frame clock: the counter saturates at
`kbd_debounce` (1…6, default 2) and a key needs that many matching frames to
press and the same number of open frames to release. It saturates at the
setting, not at a fixed ceiling — an unbounded counter made every release take
as long as the key had been held, which showed up as sluggish typing, swallowed
repeats and keys that kept auto-repeating. The keyboard endpoint advertises a
1 ms interrupt interval, so a state change leaves the board on the next host
poll rather than up to 10 ms later.

Gaming mode (`0x79`, byte 2) does one thing: while it is on, usage `0xE3` -
the left Windows key - is dropped from the report. The right GUI key still
works, and no other key or timing changes. It is a Win-lock, not a different
scan policy.

Eight more ways a key could repeat, stick or vanish were closed in the same
pass:

* A report that found the endpoint busy used to wait for the 8 ms housekeeping
  pass. `keyboard_report()` now leaves `kbd_dirty` set until every endpoint has
  taken the new state, so the main loop retries it on the next 1 ms tick.
* Reports are built **before** the LED frame is rendered (about 2 ms of work),
  instead of after it.
* The NKRO bitmap only describes usages `0x00…0x6F` plus modifiers `0xE0…0xE7`.
  A remap or macro byte outside those ranges used to be OR-ed into the bitmap
  at face value, running past `rep_nk[]` and overwriting the cached report, the
  boot report and the media report. Those bytes are now ignored at report time.
* A protocol switch (boot ⇄ report) and a host reconfiguration both invalidate
  the cached report and clear a stale endpoint-busy bit. Previously a key held
  across the switch was never re-announced, and a stuck busy bit made the
  keyboard silent until it was replugged.
* Settings writes wait for a gap between keystrokes (up to 2 s) before starting
  the flash write, so the multi-millisecond stall cannot swallow a key press.
* Arming a report is now atomic against the USB interrupt. The main loop and
  the completion handler both read-modify-write the endpoint register to set
  the ACK bit and flip the data toggle; a completion in the middle of the arm
  dropped one of the two writes. A lost ACK left the endpoint NAKing for ever,
  and a lost toggle made the host treat every later report as a duplicate — in
  both cases the keyboard looked stuck or dead until it was replugged.
  `usb_kbd_send()`, `usb_consumer_send()` and `raw_hid_send()` now hold
  interrupts off for the last few cycles of the arm.
* `CLEAR_FEATURE(ENDPOINT_HALT)` re-arms automatic toggling on the raw-HID and
  consumer endpoints. It used to write the handshake bits alone, which
  silently turned automatic toggling off, so every reply after a halt clear
  carried the same data toggle and the host discarded all of them.
* The consumer (media) endpoint still advertised a 10 ms polling interval and a
  32-byte maximum packet for a 2-byte report. It now matches the keyboard
  endpoint: 1 ms, 2 bytes, so volume and playback keys are not ten times slower
  than a normal keystroke.

Four more turned up in the follow-up audit, all of them in the same family:

* The consumer endpoint toggles its data bit in hardware, and the completion
  handler flipped it a second time. The two cancelled out, so every media
  report after the first carried a toggle the host had already seen and was
  discarded as a retransmission — a volume key repeated its first action or did
  nothing until replug. The handler no longer touches the bit (it never did for
  the raw-HID endpoint, which is why that path was fine).
* Direct mode holds the frame that is already on screen until OpenRGB sends its
  first colour, so nothing is published in that window. The "interrupts arrive
  but no frame appears” safety fallback counted that pause, and after 60 ms it
  shut the LED engine down for the rest of the session. `led_paused()` now lets
  the fallback ignore a deliberate pause.
* `SET_PROTOCOL` is a per-interface request, but the report layout is one
  global choice. Acting on it for the raw-HID or consumer interface dropped the
  keyboard to its six-key boot report; only interface 0 owns it now.
* Clearing a halt on the keyboard endpoint resets the data toggle and discards
  whatever report was queued. The cached copy still said it had been
  delivered, so a lost release was never re-sent and the host kept the key
  auto-repeating. The clear now forces a full state report.

The two extra commons are **COM30/COM31** (`P3.6`/`P3.7`, the SPI1 pins, unused
here). COM16..COM29 are the 14 matrix columns; COM30/31 carry the back light.
They are scanned but never sampled as matrix columns — forcing those pins low
would fight the engine's own common drive and there is no row to read there.

The LED interrupt runs at the **high** priority level (`IP_EX |= bIP_PWM_I2C`).
The inhibition window is only ~22 us long and a common that is serviced late is
visibly dimmer, so a USB or timer interrupt landing in the wrong place used to
show up as a flickering or dim row. The LED ISR is short enough that preempting
USB costs nothing.

## On-keyboard controls

| combo | action |
|---|---|
| Fn + `\` | cycle effects (forward) |
| Fn + `[` / `]` | previous / next effect |
| Fn + **↑** | brightness up |
| Fn + **↓** | brightness down (steps down to 0 = off) |
| Fn + **←** | effect animation slower |
| Fn + **→** | effect animation faster |
| Fn + **B** | rear/side LEDs on or off |

The arrow keys do not emit PageUp/PageDown/Home/End while Fn is held — they
become pure lighting controls, like a normal gaming keyboard. Animation speed is
`led_speed`, 1 (slow) … 8 (fast), default 4.

These keys change the lighting in force and do **not** write the data flash. A
flash write holds the CPU while a block is erased and programmed, which stops
the LED scan for about 160 ms — on the panel that is the whole keyboard
blinking. So a Fn adjustment lasts until the keyboard is unplugged, and the
stored values are what come back after a replug. The app is what stores
lighting: **Save to flash** (Advanced) writes the values in force, and that is
when a longer stop is expected because you asked for a save. Apply only sends
the state to the panel and leaves the flash alone. The per-channel trim below
is the one exception.

Effects: 35 firmware effects (off, solid, breathing, rainbow, aurora, wave,
reactive, spectrum, rain wave, colour wave, gradient, spiral, checker, strobe,
alternate, twinkle, starlight, fire, ocean, sunset, vapour, matrix, snow,
ripple, breathing rainbow, heartbeat, scanner, laser, pulse, cycle row, random
fade, heatmap, rain reactive, candy) plus a per-key **Custom** picture that is
stored in its own flash block.  Direct mode is the OpenRGB per-LED buffer: it
lives on the host, is re-sent by OpenRGB or KeyForge, and is not written to the
flash.  In Direct the six rear LEDs follow the host too, showing the average of
their six colours at the rear-light level, because the strip is driven from one
state.

Two diagnostic effects stay reachable from the keyboard: **test** paints the
five key rows red/green/blue/white/orange, and **channel walk** lights exactly
one channel (one row line) at a time so a row can be trimmed with Fn + ↑/↓.

Effect, brightness, speed, the back-light switch/level/colour/mode, start-up
animation and pace, white balance, debounce, the base-layer remap, the Custom
picture and the four macro slots all live in the CH555 Data-Flash and are
restored at power-on (`src/settings.c`).  The save is debounced by 1.5 s so
holding a combo costs a single erase.  A save compares each 64-byte page with
the flash first and skips the pages whose bytes already match, so "Save to
flash" only stops the CPU for the pages that actually changed (measured: 0.18 s
for a changed lighting block, 0.03 s when nothing changed).  Every save is read
back; if the first write window does not land, the block is rewritten one byte
at a time, so a save that reports success really is on the flash.

The back light (`led6`–`led11` on this board) lives on **COM30/COM31**, the
two commons nothing else scans.  It is driven as its own frame record, so it
has an on/off switch, a 0..127 level, its own RGB colour and its own animation
(`led_back`, `led_back_level`, `led_back_rgb`, `led_back_mode`).  Fn+B toggles
it; KeyForge's Lighting page sets the level, colour and animation.

Nine rear animations are available: solid, breathe, heartbeat, pulse, rainbow,
reactive, strobe, sparkle and wave.  All six rear LEDs are driven from a single
state computed once per frame, so a pattern can never tear between commons and
every mode is guaranteed to stay in time.  Their physical order along the strip
is not known, which is why the modes animate in time rather than "chasing" from
one end: that is a hardware fact, not a shortcut.

One physical caveat: some of these panels only light the strip when it is
driven hard, so very low levels or strongly tinted colours can look dark.  The
firmware honours the value it is given — it does not secretly substitute white.

### Start-up animation

`led_boot` picks what the panel does for the first ~1.9 s after power reaches
it: **off**, **sweep** (a warm diagonal wipe across the keys), **rainbow**
(a diagonal colour wipe), **fade** (the whole panel rising from black to warm
white), **scan** (a bright column travelling left to right) or **pulse**
(three warm whole-panel pulses).  `led_boot_pace` scales that to fast (70 %),
normal (100 %) or slow (145 %).  Both live in the settings block, so the
animation is replayed identically at every power-on.  `0x7B` replays it on
demand without a replug, which is what the app's "Write and play" button
uses.

### Per-channel brightness trim

Panels are not perfectly even from row to row, so every channel has a trim in
`led_gain[8]` (128 = unity, 255 = 2×, 0 = dark). They can be calibrated from
the keyboard itself: select the channel-walk effect (Fn + `\` cycles; it is
the last one), wait for the row you want to adjust to light up, then press
**Fn + ↑ / Fn + ↓** to trim it. This is the one Fn control that is written to
the data flash, because the trim cannot be set from the host: a save follows
about 1.5 s after the last step and the panel stops for about 160 ms while it
lands, so the trim survives a power cycle. While the channel-walk effect is
selected, Fn + ↑/↓ adjust that channel's trim instead of the global brightness;
every other effect still uses Fn + ↑/↓ for brightness.

Colour resolution matters here: `LED_CYCLE` must keep `bLED_COLOR_CYC` clear so
the colour registers stay 8 bit wide. With the 7-bit setting the effects and
OpenRGB wrote 8-bit values into a 7-bit field, which crushed everything towards
half duty and made keys whose colours sat just above a power of two look dim or
almost black.

The intensity PWM cycle is 6 bit (`bLED_INTEN_CYC1`, 64 clocks). That is what
pays for the two extra commons: the inhibition timer counts in intensity cycles,
so the per-common slot drops from 1365 us to ~700 us, and 16 commons still fit
in 11.2 ms (~89 Hz) instead of 14 commons in 19 ms (~52 Hz). `poke()` shifts the
0..127 intensity range down to the 6 bits the record carries, so the duty cycle
— and therefore the brightness — is unchanged.

The settings block is versioned. `EE_VER` is now 5; a block written by an older
firmware is migrated in place — the values it already has are kept, and only the
fields added since (start-up animation, pace, rear animation) fall back to their
defaults. The checksum is computed over the field set of the version that wrote
the block, so a v4 block is accepted instead of being wiped.

### Scan safety net

If the LED inhibition interrupts arrive but a complete matrix frame never does,
the firmware shuts the RGB engine down and switches to plain GPIO column
scanning. A lighting fault therefore cannot take the keyboard down with it.

## OpenRGB

The keyboard speaks OpenRGB's QMK protocol **revision D** on HID **interface 1**
(usage page `0xFF60`, usage `0x61`, EP2 OUT / EP3 IN, 64-byte
reports), which OpenRGB autodetects as a per-key keyboard.

- **Zones:** `Keyboard` (63 key backlights, flag `0x04`, named from their HID
  usages) and `Underglow` (the six rear/side LEDs, flag `0x02`).
- **LED ids:** 0–62 are the keys in the same row-major order as `keymap.c`,
  63–68 are the underglow LEDs. `SET_SINGLE_LED` / `SET_LEDS` use those ids.
- **Modes offered:** `Direct`, `Static`, `Breathing`, `Cycle All`, `Hue Wave`
  and `Solid Reactive`. OpenRGB numbers the modes it builds by their position
  in that list (1, 2, 3 …), not by their QMK id, and assigns Direct last
  because it is created last even though its UI entry appears first — so the
  numbers on the wire are `Static` 1, `Breathing` 2, `Cycle All` 3, `Hue Wave`
  4, `Solid Reactive` 5 and `Direct` 6. The firmware speaks those positions.
  Direct is OpenRGB's per-LED editor; the others select the matching onboard
  effect, so the host and the keyboard agree on what is on screen.
- **Brightness / speed:** the mode `value` byte drives `led_brightness`, the
  `speed` byte drives `led_speed` (0…255 -> 1…8). Colours are converted at full
  value, so host-set colours are exactly as bright as onboard ones. In Direct
  the `value` byte is only treated as a dimmer when bit 7 of the `save` byte is
  set, because OpenRGB's driver sends `value = 0` when it selects Direct and
  would black the panel out otherwise. KeyForge sets that bit for its Levels
  slider; OpenRGB never does, so selecting Direct there leaves the current
  brightness alone. The app's Levels slider steps in 2 up to 254, the even
  values the 7-bit `led_brightness` register can actually hold.
- **Saving:** the `save` byte of `SET_MODE` (OpenRGB's manual-save button)
  persists the effect, brightness and speed to the data flash. Slider changes
  alone never write the flash, so dragging a slider cannot wear it out.
- **Priority:** any lighting key on the keyboard takes the lights back from the
  host (`openrgb_leave_direct()`), so Fn controls keep working while OpenRGB is
  connected.

Test it without OpenRGB:

```sh
tools/openrgb-test.py --info       # protocol, zones, LED table
tools/openrgb-test.py --static     # paint all 69 LEDs and read them back
tools/openrgb-test.py --modes      # walk the advertised modes
tools/openrgb-test.py --underglow  # light one strip LED at a time
tools/openrgb-test.py              # 6 s moving rainbow
```

## KeyForge app

`app/` is the desktop control surface.  `app/server.py` owns every USB/hidraw
operation (it is the only process that talks to the board) and serves both the
API and the built UI; `app/ui` is React + TypeScript + Tailwind, with all
hardware mutation behind `src/lib/api.ts`.

```sh
python3 app/server.py            # http://127.0.0.1:8765/
cd app/ui && npm run dev         # Vite dev server, /api proxied to 8765
```

Pages map to firmware commands, one for one:

| page | what it writes |
|---|---|
| Lighting | effect / HSV / brightness / speed, Direct per-key colours, Custom picture, rear light |
| Effects | the same effect state, plus the exact parameter surface of each effect |
| Sound | no firmware command: host-rendered frames (`0x09`) over the Direct path, seven audio-reactive modes |
| Keymap | `0x73` per-key HID usage, base layer only (0xFF = factory, 0x00 = disabled) |
| Macros | `0x78` four slots, browser-recorded (usage, hold) steps, board-side playback |
| Profiles | host-side JSON presets of the lighting, board config and selection (not the keymap or macros) |
| Advanced | `0x79` debounce / gaming / calibration / rear light / start-up animation, diag, `0x76` save, `0x7A` restart, `0x7B` boot preview, guided flash |

A key with a macro bound does not type its own character: the recorded steps
are the whole output of that key, and they stay the whole output even after
the list has finished while the switch is still held.  Clear the slot, or bind
it to a different key, to get the key's normal usage and remap back.  One macro
plays at a time and the lowest-numbered slot bound to a key wins, which is why
the Macros page warns when two slots share a trigger.  Fn cannot be a trigger:
it is the layer key, and a macro bound there would fire on every lighting
chord, so the app hides it and the firmware rejects it.  For the same reason
the keymap page does not offer Fn as a remap target: `keyboard_report()` skips
it and reads it as the layer key, so a stored usage there would never be sent.
`0x76` writes the blocks before it answers, so a success reply means the bytes
are already on the flash rather than merely queued behind a debounce.

The effect preview is a direct port of `pixel()` from `src/led.c` (hue sector
maths, `tri16`, `mix8`, `scale8`, the 6-bit dimmer and the white balance), so
the screen and the panel agree at every brightness and speed.  Only effects the
firmware can actually draw are listed; the wire-mode numbers mirror the
OpenRGB `EFFECT_WIRE` table exactly.

The Sound page is the one deliberate exception to "the board does it".  The
CH555 has no microphone, so `app/audio.py` records the monitor of the machine's
default output with `parec`, turns it into 14 logarithmic bands plus a level,
and streams one colour per LED.  Seven modes (`spectrum`, `pulse`, `wave`,
`meter`, `spark`, `ripple`, `comet`) and six palettes are rendered on the host;
the board is switched to Direct for the duration and returned to the effect it
was showing when playback stops.  Applying lighting, releasing the device or
closing the service also stops it, so no stale frames are left on the panel.

The UI only offers controls that map to a real command.  The private raw-HID
commands behind them are:

| command | payload | meaning |
|---|---|---|
| `0x70` | effect hue sat val speed save | set effect + parameters |
| `0x71` | — | read effect, brightness, speed, back light, debounce, cal, rear mode, boot mode, boot pace (20 bytes) |
| `0x72` | first count | read remap entries |
| `0x73` | index usage | write one remap entry |
| `0x74` | first count | read the Custom picture |
| `0x75` | n (index r g b)… | write the Custom picture |
| `0x76` | — | write settings, remap, picture and macros now; replies after every block reads back |
| `0x77` | slot | read one macro slot |
| `0x78` | slot key n (usage hold)… | write one macro slot |
| `0x79` | debounce gaming back level cal rgb back rgb boot bootpace backmode | write config (14 bytes) |
| `0x7A` | — | restart the firmware (the ROM bootloader cannot be held from software) |
| `0x7B` | — | replay the selected start-up animation |
| `0x7E` | — | diagnostic counters (layout 5, 32 bytes) |
| `0x6D` | — | write the settings block now, no debounce (repair / test) |

## Firmware layout

| file | purpose |
|---|---|
| `src/main.c` | init, main loop, interrupt vectors (SDCC emits vectors only from the module that defines them) |
| `src/usb.c` | USB device stack: descriptors, EP0 state machine, NKRO/boot reports, consumer reports |
| `src/keyboard.c` | matrix decoding, debounce, layer/report composition |
| `src/keymap.c` | keycode tables per layer, matrix → key index table |
| `src/led.c` | RGB LED engine (DMA pump, inhibition ISR, 35 effects, start-up animations, rear-light animations) |
| `src/openrgb.c` | OpenRGB QMK protocol D handler, zones, modes, direct mode |
| `src/settings.c` | versioned data-flash block: effect, brightness, speed, strip, boot animation, channel trims |
| `src/board.c` | 24 MHz clock, delays, 1 ms Timer0 tick |
| `src/matrix.c` | 40-pin brute-force scanner, only used by the detector build |
| `include/ch555.h` | SDCC header generated from WCH's Keil `CH555.H` |

## Build

```sh
make keyboard     # -> build/keyboard.hex  (production)
make detect       # -> build/detect.hex    (matrix detector)
```

Toolchain: `sdcc` (tested 4.6.0) + `packihx`.

## Flashing

Flashing is done from the app: **Advanced → Firmware → Flash**.  The app owns
the whole job (patched `wchisp`, verification, progress, retry-on-failure); the
only thing it cannot do is press the power switch, because of the hardware
constraint below.

The CH555 ROM bootloader only serves ISP after a **power-on reset**.  Its
`GLOBAL_CFG.bBOOT_LOAD` bit is read-only: it is 1 after power-on and is cleared
by *any* software reset.  So a software jump to `0xF400` (which the firmware
still implements in `src/board.c:boot_enter`, reachable as `0x7A`) re-enters
the bootloader with the bit clear, and it resets straight back into the
application — measured on this board, it never exposes `4348:55e0`.  Software
bootloader entry is therefore a hardware impossibility here, not a firmware
bug.

What the app does instead is take over every part of flashing except the one
physical action: **Advanced → Flash firmware** starts a job that waits for
`4348:55e0` to appear, and the instant the board power-cycles it runs the
patched `wchisp` on `build/keyboard.hex` and verifies the write.  The same thing
by hand:

```sh
tools/flash-on-replug.sh build/keyboard.hex   # fit the boot jumper, then power-cycle
```

Two practical notes:

* **Start the job first, then power-cycle.**  The ROM loader only waits a short
  time for a host handshake; if nothing is listening it clears `bBOOT_LOAD` and
  starts the application, which looks like "replugging does nothing".
* **This unit needs the boot jumper.**  A plain unplug/replug leaves the
  application running, because VBUS never drops long enough (or the loader
  window closes before the host enumerates).  Fit the jumper, then plug the
  cable in or replug it, so the chip sees a real power-on reset with the jumper
  in place.  The job polls for the device and does not care how it got there.
  If the write fails, it asks for another power cycle rather than retrying a
  board that has already left the loader.

`0x7A` on its own is exposed in the app as **Restart keyboard**: it clears the
USB block and jumps, which the bootloader turns into a clean restart of the
application.

`tools/wchisp/target/release/wchisp` is a locally patched build of wchisp 0.3.0
with five CH555 fixes:

1. ISP bulk endpoints are detected from the descriptors (this bootloader uses
   OUT `0x02` / IN `0x83`, not `0x82`).
2. `SET_CONFIGURATION` is skipped when the configuration is already active.
3. Both ISP endpoints get `clear_halt()` after `claim_interface()`.
4. The ISP XOR key uses all 8 UID bytes for the CH555 (checksum `0x7d` here).
5. Responses are accepted on valid framing rather than on the status byte: this
   bootloader answers `0x00`, `0x5A` or `0xFC` for the same successful request,
   always with the real data in the payload (`55 11` = chip id, `fe 00` = no
   data). The written image is verified against the hex afterwards.

## Fn layer

| combo | result |
|---|---|
| Fn + ESC | `` ` `` |
| Fn + 1..0, -, = | F1..F12 |
| Fn + BSPC | Delete |
| Fn + U / I / O / P | Insert / PrintScreen / ScrollLock / Pause |
| Fn + L | NumLock |
| Fn + UP / DOWN / LEFT / RIGHT | PageUp / PageDown / Home / End |
| Fn + Q / W / E / R / T / Y | Mute / Vol− / Vol+ / Prev / Next / Play-Pause |

## Host tools

- `host/keytest.py` — decode evdev events from the keyboard (verification).
- `host/logger.py`, `host/capture.py` — matrix detector clients.
- `host/map.json` — recovered wiring.
- `tools/led-capture.py` — watch the LED engine from the inside: frame rate,
  blackouts, stalls, bad DMA, phase and colour jumps.
- `tools/effect-sweep.py` — play every effect in turn and run those same checks
  on each one, so a single bad effect cannot hide in an average.  Stop the
  service first (`fuser -k -n tcp 8765`): two readers on the raw-HID endpoint
  steal each other's replies.  Effects that switch colour by design (checker,
  strobe, sparkles, drops, sweeps) are listed in `DISCRETE` and their colour
  changes are reported as switching rather than as faults.
