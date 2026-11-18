# KeyForge

Desktop-style control surface for the Velvet Mech keyboard.

```
app/
  server.py          local service: static files + JSON API + libhidapi
  profiles.json      saved profiles and keysets (created on first save)
  ui/                React + TypeScript + Tailwind (Vite)
    src/pages/       Lighting, Effects, Keymap, Macros, Sound, Profiles, Advanced
    src/components/  keyboard, controls, chrome
    src/lib/         layout (generated), colour maths, effect preview
    scripts/         gen-layout.mjs - keyboard geometry from host/map.json
```

## Run

```sh
cd app/ui && npm install && npm run build   # once, or after UI changes
cd .. && python3 server.py                  # http://127.0.0.1:8765/
```

`server.py` opens the browser. Add `--no-browser` to stop that, `--port N` to
move it, and `--dev` to point the browser at the Vite dev server instead
(`cd ui && npm run dev`, proxying `/api` to this process).

## How the pieces talk

* The browser only ever speaks HTTP to `127.0.0.1`. USB lives in `server.py`,
  which owns one hidapi handle on usage page `0xFF60` / usage `0x61`.
* Applying an effect sends one `SET_STATE` packet (`0x70`) with effect id, HSV,
  brightness, speed and the save byte. Direct is the OpenRGB path instead: a
  mode packet (`0x07`) followed by `SET_LEDS` chunks of 15 LEDs.
* Board settings (debounce, gaming, rear light, calibration, start-up and rear
  animations) go through `SET_CONFIG` (`0x79`); remap, Custom picture and
  macros have their own read/write commands and are persisted by `0x76`.
* The UI previews effects itself (`src/lib/effects.ts` mirrors `src/led.c`), so
  animations stay smooth without a packet per frame.

## Layout data

`src/lib/keyboard-data.ts` is generated, not hand-written:

```sh
cd app/ui && node scripts/gen-layout.mjs
```

It reads `host/map.json` for matrix positions and repeats the firmware's keymap
tables, then asserts every row is exactly 15 u wide. If the layout or keymap in
`src/keymap.c` changes, regenerate instead of editing the file.

## Deliberate omissions

The firmware is the source of truth, so the UI does not offer anything it cannot
do. There is no polling-rate control, the Fn layer is compiled in, remapping
accepts keyboard-page usages only (the NKRO report cannot carry media or mouse
usages), and the six rear LEDs are driven together so they cannot be addressed
one at a time. Sound-reactive lighting is rendered on the host because the
board has no microphone, and macros are recorded here but played back by the
board.
