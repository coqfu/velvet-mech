#!/usr/bin/env python3
"""Play every firmware effect on the board and audit the LED engine.

    tools/effect-sweep.py                      # all effects
    tools/effect-sweep.py 3 7 21               # just these ids
    tools/effect-sweep.py --seconds 6 --json out.json

One HID client only.  The service and this tool both read the same raw-HID
endpoint and a second reader steals replies - which shows up as dropped
commands and lost diagnostic samples, not as LED faults.  Stop the service
first:

    fuser -k -n tcp 8765

For each effect it applies the state, then runs the checks from
`tools/led-capture.py` in-process, so the numbers are that tool's.

What counts as a fault:

    BLACKOUT     a lit column went dark
    PHASE FREEZE the animation clock stopped while frames advanced
    STALL        no frame was published for three samples
    BAD DMA      the engine streamed from an address that is not a frame
    RENDER SLOW  a frame took 18 ms or more of the 19 ms budget
    DIRECT HOLD  Direct mode with no colour packet

COLOUR JUMP is a different matter: the detector treats a per-column colour
change of more than 24 counts in 50 ms as a snap rather than motion, which is
right for smooth effects (rainbow, wave, gradient, ocean...) and wrong for
effects that are *supposed* to switch colour between frames - a checkerboard,
a strobe, sparkles, falling drops, a sweeping beam.  Those are listed in
DISCRETE below; for them a colour jump is reported as SWITCHING and does not
count against the effect.  An effect that is not in that list and jumps is a
real finding.
"""

import argparse
import contextlib
import ctypes
import importlib.util
import io
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURE = os.path.join(HERE, "led-capture.py")

HUE, SAT, VAL, SPEED = 20, 255, 200, 4
CMD_SET_STATE = 0x70

EFFECTS = [
    "Off", "Solid", "Breathing", "Rainbow", "Aurora", "Wave", "Reactive",
    "Spectrum", "Rain Wave", "Colour Wave", "Gradient", "Spiral", "Checker",
    "Strobe", "Alternate", "Twinkle", "Starlight", "Fire", "Ocean", "Sunset",
    "Vapour", "Matrix", "Snow", "Ripple", "Breath Rain", "Heartbeat",
    "Scanner", "Laser", "Pulse", "Cycle Row", "Random Fade", "Heatmap",
    "Rain React", "Candy", "Custom",
]

# Effects whose own definition switches a column's colour between frames.
DISCRETE = {
    3,   # Rainbow: hue offset moves quickly at speed 4
    7,   # Spectrum: hue bands per row - a column's brightest row changes band
    12,  # Checker: two-state checkerboard
    13,  # Strobe: whole-board flash
    14,  # Alternate: rows alternate two states
    15,  # Twinkle: sparkles appear
    16,  # Starlight: stars appear
    17,  # Fire: random flicker
    21,  # Matrix: drops fall
    22,  # Snow: flakes drift to new keys
    25,  # Heartbeat: fast double beat
    26,  # Scanner: beam enters and leaves columns
    27,  # Laser: beam sweeps rows
    28,  # Pulse: ring expands
    29,  # Cycle Row: one row lit at a time
    30,  # Random Fade: per-key random fades
    18,  # Ocean: swell crosses the 24-count threshold at some phases
    33,  # Candy: colour blocks
}

FAULTS = ("blackout", "phase", "stall", "dma", "render", "hold")


def load_capture():
    spec = importlib.util.spec_from_file_location("led_capture", CAPTURE)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["led_capture"] = mod
    spec.loader.exec_module(mod)
    mod.running = True
    return mod


class Recorder(object):
    """Capture with an extra log of (time, published frames).

    Capture.fps needs a whole second of uninterrupted diagnostic samples, so a
    short run or a couple of lost replies can leave it at zero even though the
    engine is publishing normally.  The sweep computes the rate from every
    sample it did get instead.
    """

    samples = None

    def __init__(self, base, dev, args):
        self.cap = base(dev, args)
        self.cap.check_frames_orig = self.cap.check_frames
        self.samples = []
        cap = self.cap

        def check_frames(d):
            self.samples.append((time.monotonic(), d["frames"]))
            self.cap.check_frames_orig(d)

        # Forward everything else straight through.
        self.__getattr__ = lambda name: getattr(cap, name)
        cap.check_frames = check_frames
        self.cap = cap

    def run(self):
        self.cap.run()

    def rate(self):
        if len(self.samples) < 2:
            return 0.0
        (t0, f0), (t1, f1) = self.samples[0], self.samples[-1]
        span = t1 - t0
        if span <= 0.2:
            return 0.0
        return ((f1 - f0) & 0xFFFF) / span


def drain(t, dev):
    """Read replies nobody is waiting for, without sending anything."""
    buf = ctypes.create_string_buffer(t.PACKET)
    for _ in range(8):
        if dev.lib.hid_read_timeout(dev.handle, buf, t.PACKET, 0) <= 0:
            return


def apply_effect(t, dev, idx):
    for _ in range(3):
        drain(t, dev)
        r = dev.request([CMD_SET_STATE, idx, HUE, SAT, VAL, SPEED, 0])
        if r and len(r) > 1 and r[1] == 50:
            return True
        time.sleep(0.05)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("effects", nargs="*", type=int)
    ap.add_argument("--seconds", type=float, default=4.0,
                    help="sample each effect for this long")
    ap.add_argument("--json", help="write the per-effect results here")
    args = ap.parse_args()

    t = load_capture()
    lib = t.load_hidapi()
    paths = t.find_paths(lib)
    if not paths:
        raise SystemExit("no raw HID interface (usage page 0xFF60) found")

    dev = t.Dev(lib, paths[0])
    print("board %s" % paths[0], flush=True)

    cap_args = argparse.Namespace(seconds=args.seconds, hz=20, diag_hz=5,
                                  print_every=1e9, trace=False)
    ids = args.effects or list(range(len(EFFECTS)))
    rows = []

    for idx in ids:
        name = EFFECTS[idx]
        if not apply_effect(t, dev, idx):
            print("%2d %-13s APPLY REJECTED" % (idx, name), flush=True)
            rows.append({"idx": idx, "name": name, "error": "apply rejected"})
            continue
        time.sleep(0.5)

        rec = Recorder(t.Capture, dev, cap_args)
        with contextlib.redirect_stdout(io.StringIO()):
            rec.run()

        cap = rec.cap
        fps = rec.rate() or cap.fps
        c = cap.counts
        faults = [k for k in FAULTS if c[k]]
        switching = c["colour"]
        if idx not in DISCRETE and switching:
            faults.append("colour")
        verdict = "ok  " if not faults else "FAULT"
        note = "" if idx in DISCRETE or not switching else ""
        if idx in DISCRETE and switching:
            note = "  (%d switching, by design)" % switching
        print("%2d %-13s %s fps %5.1f  black %d phase %d stall %d dma %d "
              "slow %d hold %d%s"
              % (idx, name, verdict, fps, c["blackout"], c["phase"],
                 c["stall"], c["dma"], c["render"], c["hold"], note),
              flush=True)
        rows.append({
            "idx": idx, "name": name, "fps": round(fps, 1),
            "counts": dict(c), "discrete": idx in DISCRETE,
            "verdict": "ok" if not faults else "fault", "faults": faults,
        })

    dev.close()

    if args.json:
        with open(args.json, "w") as f:
            json.dump(rows, f, indent=1)

    bad = [r for r in rows if r.get("error") or r.get("faults")]
    print("\n%d/%d effects clean" % (len(rows) - len(bad), len(rows)), flush=True)
    if bad:
        print("  " + ", ".join("%s: %s" % (r["name"], r.get("error") or
                                          ",".join(r["faults"])) for r in bad),
              flush=True)
        raise SystemExit(1)


main()
