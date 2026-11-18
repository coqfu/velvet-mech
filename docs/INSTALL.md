# Install and first run

This guide takes a fresh Linux machine to a working keyboard: dependencies, the
app, the firmware and the flashing step. Everything here was run on the machine
the project was developed on (Arch Linux); the only distribution-specific parts
are package names.

## 0. What you need

**Hardware.** One board: the **iGear Raptor**, a 63-key 64% keyboard built on
the WCH **CH555**. That is the only model this firmware supports. The matrix,
the LED channel wiring, the rear-light commons and the boot jumper were all
measured on that unit, so a different keyboard will not work without repeating
that work (see "Porting" at the end).

You also need the **USB cable** and something to short the **boot jumper** for
the first flash — a jumper cap, a bent paperclip or tweezers. The boot jumper is
required on this board: a plain replug never exposes the ROM loader.

**Software.**

| need | why | Arch | Debian/Ubuntu |
|---|---|---|---|
| `python3` | the app and the service | `pacman -S python` | `apt install python3` |
| `hidapi` | USB access from the app | `pacman -S hidapi` | `apt install libhidapi-hidraw0` |
| `sdcc` | only to build the firmware | `pacman -S sdcc` | `apt install sdcc` |
| `wchisp` | only to flash | `cargo install wchisp` | `cargo install wchisp` |
| `node` + `npm` | only to change the UI | `pacman -S nodejs npm` | `apt install nodejs npm` |

The built UI is committed, so running the app needs Python only. The prebuilt
firmware is committed too, so flashing does not need `sdcc` either.

## 1. Install the app

```sh
git clone https://github.com/CodingHxx/velvet-mech.git
cd velvet-mech
./install.sh
```

That copies the app, the shared layout data, the firmware image and the sources
into `~/.local/share/keyforge`, and puts a `keyforge` launcher in `~/.local/bin`
plus a desktop entry in `~/.local/share/applications`. Nothing else is touched.

**USB permissions.** On a normal desktop the keyboard node is owned by root, so
the app cannot open it. Install the rule once:

```sh
./install.sh --udev
```

which writes `/etc/udev/rules.d/99-keyforge.rules` for both the keyboard
(`1209:c555`) and its ROM bootloader (`4348:55e0`). Replug the keyboard after it.

## 2. Run it

```sh
keyforge                 # serves http://127.0.0.1:8765/ and opens the browser
keyforge --no-browser    # just serve
keyforge --port 8800     # different port
```

The app is a local web UI because the browser is the most portable way to draw
the keyboard, the effect previews and the graphs. It binds `127.0.0.1` only and
talks to the board from Python; the page itself never sees USB.

First run: the Lighting page should show the board as connected with its LED
count (63 keys + 6 rear = 69) and its saved effect. If it says disconnected,
the udev rule or the cable is the first thing to check.

## 3. Use it

| page | what it does |
|---|---|
| Lighting | paint per-key colours, set the effect, brightness and speed, save to flash |
| Effects | the 35 firmware effects, their parameters, the six rear animations |
| Keymap | click a key, pick a usage, remap it; reset to factory |
| Macros | bind a macro slot to a key, record steps with hold times, play them from the board |
| Sound | host-rendered audio-reactive lighting streamed over the Direct path |
| Profiles | save/load lighting + board settings + custom picture |
| Advanced | debounce, gaming mode, calibration, start-up animation, flash tools, diagnostics |

Two behaviours that look like faults but are not:

* **Fn lighting keys do not write the flash.** A flash write stops the LED scan
  for ~160 ms and the whole panel blinks, so the Fn keys change the lighting in
  force and leave the stored copy alone. `Save to flash` (Advanced) is what
  persists, and that is when the stop is expected.
* **A save that changes nothing is instant.** The firmware compares each 64-byte
  page with the flash and skips identical ones.

## 4. Flash the firmware

The app's Advanced page has a guided flash: it starts a job that waits for the
ROM bootloader, then writes `build/keyboard.hex` with `wchisp` and verifies it.

1. Press **Flash** in the app.
2. Fit the **boot jumper** on the board's two pins.
3. Unplug the keyboard, wait a second, plug it back in.
4. The app reports `Bootloader found`, then `Verify OK`.
5. Remove the jumper and replug. The board comes back as `Velvet Mech`.

Manual equivalent, if you prefer the shell:

```sh
tools/wchisp/target/release/wchisp flash build/keyboard.hex   # or just: wchisp
```

with the jumper fitted and the board freshly replugged, then remove the jumper
and replug.

There is no software way into the bootloader on this chip: `bBOOT_LOAD` is
read-only and cleared by any software reset. The jumper and a power cycle are
the only entry.

## 5. Build the firmware

```sh
make keyboard      # build/keyboard.hex, the image the app flashes
make detect        # a matrix-only diagnostic build
make clean
```

`make keyboard` runs SDCC per file, links the `.rel` files and checks that no
interrupt vector is overlaid. It prints the image and fails loudly on an overlay.

## 6. Build the UI (only if you change it)

```sh
cd app/ui
npm install
npm run build      # writes app/ui/dist, which the service serves
```

## 7. OpenRGB

The board speaks the QMK/OpenRGB raw-HID protocol on usage page `0xFF60`, usage
`0x61`: 63 key LEDs plus a 6-LED underglow zone, modes Static, Breathing, Cycle
All, Hue Wave, Solid Reactive and Direct. Close KeyForge before letting OpenRGB
take the device — both want the same raw HID handle.

## 8. Troubleshooting

| symptom | cause |
|---|---|
| app says disconnected, `keyforge` was just installed | udev rule missing, or the keyboard is on the bootloader |
| `libhidapi not found` | install `hidapi` |
| flash job waits for ever | jumper not fitted, or fitted after the power cycle instead of before |
| `wchisp` not found | `cargo install wchisp`, or clone ch32-rs/wchisp into `tools/wchisp` |
| panel blinks when you press Save | expected: it is the flash write, once per save |
| brightness set with Fn is back after a replug | expected, see "Use it" above |

Diagnostics that answer most questions without guessing:

```sh
curl -s http://127.0.0.1:8765/api/diag     # frames, fps, effect, brightness, save counters
curl -s http://127.0.0.1:8765/api/flash    # flash job state and output
```

## 9. Uninstall

```sh
./uninstall.sh
```

## Porting to another keyboard

Do not expect this firmware to run on a different board. It carries the measured
matrix map (`src/keymap.c`, `host/map.json`), the LED commons/channels
(`src/led.c`), the rear-light exceptions and the jumper behaviour of this one
unit. Porting means re-measuring all of that: drive one column at a time and
read the rows to recover the matrix, light one channel at a time to recover the
LED map, then build the tables from those measurements. The tools used to do it
are in `tools/` (`led-capture.py`, `openrgb-test.py`, `ispprobe.py`).
