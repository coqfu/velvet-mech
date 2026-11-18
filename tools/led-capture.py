#!/usr/bin/env python3
"""Watch the LED engine from the inside, at a rate the CPU can afford.

    tools/led-capture.py [--seconds N] [--path /dev/hidrawN]

Two private commands on the raw HID interface:

    0x7E  diag   counters and state (frames published, render time, clock)
    0x7D  sig    one 60-byte snapshot of the frame that is on screen:
                 14 x (brightest key channel intensity, r, g, b), animation
                 phase, clock low byte, buffer, render time

The signature reads the five key channels only.  Channels 0/6/7 are spare
slots taken by the fixed white back light, and including them made every dim
effect look white.

That is 20 requests/s, which the renderer can absorb: it shares one 8051 with
the LED PWM engine, and a 300 request/s hammer visibly starves it.

What it reports:

    BLACKOUT     a column that was lit a moment ago is all zeroes
    PHASE JUMP   the visible animation phase advanced by more than the clock
    COLOUR JUMP  a column's hue snapped to another value
    STALL        led_frames stopped advancing
    BAD DMA      the engine streams from an address that is not a frame base
    RENDER SLOW  a frame took more than 18 ms of the 19 ms budget
    DIRECT HOLD  Direct mode entered, no colour packet yet (keeps last frame)
    STATE        effect / brightness / speed / back / direct changed

Replies can be stolen by another raw HID client (the OpenRGB UI), so every
reply is matched against the command that was sent before it is believed.
"""

import argparse
import ctypes
import datetime
import signal
import sys
import time

PACKET = 65
CMD_SIG = 0x7D
CMD_DIAG = 0x7E

DIAG_LEN = 22
SIG_LEN = 60
COLS = 14                       # signature columns (matrix commons only)
LED_COLS = 16                   # commons the engine scans: +COM30/31 back light
FRAME_BYTES = LED_COLS * 32     # bytes between the two ping-pong frames

NAMES = [
    "ver", "irqs", "frames_lo", "frames_hi", "dma_lo", "dma_hi",
    "show", "work", "ready", "is_on", "back", "bright", "effect", "speed",
    "direct", "have_colors", "boot", "t_lo", "t_hi", "color_r",
    "render_ms", "phase", "reset",
]

EFFECT_NAMES = {
    0: "off", 1: "solid", 2: "breath", 3: "rainbow", 4: "aurora",
    5: "wave", 6: "reactive", 7: "test", 8: "chan",
}

BARS = " .:-=+*#%@"

running = True


def load_hidapi():
    for name in ("libhidapi-hidraw.so", "libhidapi-hidraw.so.0",
                 "libhidapi-libusb.so", "libhidapi-libusb.so.0"):
        try:
            return ctypes.CDLL(name)
        except OSError:
            pass
    raise SystemExit("hidapi not found")


def find_paths(lib):
    class Info(ctypes.Structure):
        pass

    Info._fields_ = [
        ("path", ctypes.c_char_p),
        ("vendor_id", ctypes.c_ushort),
        ("product_id", ctypes.c_ushort),
        ("serial_number", ctypes.c_wchar_p),
        ("release_number", ctypes.c_ushort),
        ("manufacturer_string", ctypes.c_wchar_p),
        ("product_string", ctypes.c_wchar_p),
        ("usage_page", ctypes.c_ushort),
        ("usage", ctypes.c_ushort),
        ("interface_number", ctypes.c_int),
        ("next", ctypes.POINTER(Info)),
    ]

    lib.hid_enumerate.restype = ctypes.POINTER(Info)
    lib.hid_enumerate.argtypes = [ctypes.c_ushort, ctypes.c_ushort]
    head = lib.hid_enumerate(0, 0)
    node = head
    out = []
    while node:
        i = node.contents
        if i.usage_page == 0xFF60 and i.usage == 0x61:
            out.append(i.path.decode())
        node = i.next
    lib.hid_free_enumeration(head)
    return out


class Dev:
    def __init__(self, lib, path):
        self.lib = lib
        lib.hid_open_path.restype = ctypes.c_void_p
        lib.hid_open_path.argtypes = [ctypes.c_char_p]
        self.handle = lib.hid_open_path(path.encode())
        if not self.handle:
            raise OSError("cannot open " + path)
        self.path = path
        lib.hid_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                  ctypes.c_size_t]
        lib.hid_read_timeout.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                         ctypes.c_size_t, ctypes.c_int]
        lib.hid_read_timeout.restype = ctypes.c_int
        lib.hid_close.argtypes = [ctypes.c_void_p]

    def close(self):
        if self.handle:
            self.lib.hid_close(self.handle)
            self.handle = None

    def request(self, data, timeout=90):
        buf = bytearray(PACKET)
        buf[0] = 0
        buf[1:1 + len(data)] = data
        if self.lib.hid_write(self.handle, bytes(buf), PACKET) < 0:
            return None
        out = ctypes.create_string_buffer(PACKET)
        n = self.lib.hid_read_timeout(self.handle, out, PACKET, timeout)
        if n <= 0:
            return None
        raw = out.raw[:n]
        if raw[0] != data[0]:
            return None             # late reply for someone else's request
        return raw


class Capture:
    def __init__(self, dev, args):
        self.dev = dev
        self.args = args
        self.t0 = time.monotonic()
        self.diag = None
        self.sig = None
        self.sig_t = None
        self.prev_cols = None
        self.prev_phase = None
        self.prev_sig_t = None
        self.frames_base = None
        self.expected_dma = set()
        self.dma_seen = {}
        self.prev_state = None
        self.lit_cols = [0] * COLS
        self.cur_int = [0] * COLS
        self.frames_seen = None
        self.frames_time = None
        self.dead_samples = 0
        self.stall_since = None
        self.hold_since = None
        self.hold_reported = False
        self.overrun_reported = 0
        self.lost = 0
        self.requests = 0
        self.latencies = []
        self.counts = {
            "blackout": 0, "phase": 0, "colour": 0, "stall": 0, "dma": 0,
            "render": 0, "hold": 0, "changes": 0,
        }
        self.fps = 0.0
        self.fps_samples = 0
        self.next_print = 0.0

    # -- output --------------------------------------------------------
    def stamp(self):
        now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        return "%s %7.2fs" % (now, time.monotonic() - self.t0)

    def say(self, kind, text):
        print("%s  %-12s %s" % (self.stamp(), kind, text), flush=True)

    def describe(self):
        d = self.diag
        if not d:
            return ""
        return ("effect=%s bright=%d speed=%d back=%d on=%d direct=%d "
                "colors=%d fps=%.0f render=%dms phase=%d dma=%04X" % (
                    EFFECT_NAMES.get(d["effect"], d["effect"]),
                    d["bright"], d["speed"], d["back"], d["is_on"],
                    d["direct"], d["have_colors"], self.fps, d["render_ms"],
                    d["phase"], d["dma"]))

    def bars(self):
        return "".join(BARS[min(len(BARS) - 1, v * len(BARS) // 128)]
                       for v in self.cur_int)

    # -- checks --------------------------------------------------------
    def check_state(self, d):
        keys = ("effect", "bright", "speed", "back", "is_on", "direct",
                "have_colors", "boot")
        state = tuple(d[k] for k in keys)
        if self.prev_state is not None and state != self.prev_state:
            self.counts["changes"] += 1
            changed = ["%s %s->%s" % (k, p, c)
                       for k, p, c in zip(keys, self.prev_state, state)
                       if p != c]
            self.say("STATE", ", ".join(changed) + "   " + self.describe())
        self.prev_state = state

    def check_dma(self, d):
        dma = d["dma"]
        if self.frames_base is None and dma:
            # Either buffer can be the first one seen, so accept the pair
            # {base, base +- 448} and treat anything else as wrong.
            self.frames_base = dma
            self.expected_dma = {(dma + FRAME_BYTES) & 0xFFFF,
                                 (dma - FRAME_BYTES) & 0xFFFF, dma}
        self.dma_seen[dma] = self.dma_seen.get(dma, 0) + 1
        if self.expected_dma and dma not in self.expected_dma:
            self.counts["dma"] += 1
            self.say("BAD DMA", "engine streams %04X, frames live at %s  %s"
                     % (dma, "/".join("%04X" % a for a in
                                      sorted(self.expected_dma)),
                        self.describe()))

    def check_frames(self, d):
        now = time.monotonic()
        if self.frames_seen is None:
            self.frames_seen = d["frames"]
            self.frames_time = now
            return
        if d["frames"] == self.frames_seen:
            self.dead_samples += 1
            if self.dead_samples >= 3 and self.stall_since is None:
                self.stall_since = now
                self.counts["stall"] += 1
                self.say("STALL", "no published frame for %d samples  %s"
                         % (self.dead_samples, self.describe()))
            return
        if self.stall_since is not None:
            self.say("RECOVERED", "publishing resumed after %.2fs"
                     % (now - self.stall_since))
            self.stall_since = None
        elapsed = now - self.frames_time
        if elapsed >= 1.0:
            delta = (d["frames"] - self.frames_seen) & 0xFFFF
            self.fps = delta / elapsed
            self.fps_samples += 1
            self.frames_seen = d["frames"]
            self.frames_time = now
        self.dead_samples = 0

    def check_hold(self, d):
        now = time.monotonic()
        if d["direct"] and not d["have_colors"]:
            if self.hold_since is None:
                self.hold_since = now
            if not self.hold_reported and now - self.hold_since > 0.3:
                self.hold_reported = True
                self.counts["hold"] += 1
                self.say("DIRECT HOLD", "host entered Direct mode with no "
                         "colour packet; last frame kept  %s" % self.describe())
        else:
            if self.hold_reported:
                self.say("DIRECT OK", "colour packets flowing  %s"
                         % self.describe())
            self.hold_since = None
            self.hold_reported = False

    def check_render(self, d):
        if d["render_ms"] >= 18 and d["boot"] == 0:
            if self.counts["render"] == 0 or \
                    time.monotonic() - self.last_render > 2.0:
                self.last_render = time.monotonic()
                self.counts["render"] += 1
                self.say("RENDER SLOW", "a frame used %d ms of the 19 ms "
                         "budget  %s" % (d["render_ms"], self.describe()))

    def check_sig(self, r):
        if len(r) < 1 + SIG_LEN:
            return
        body = r[1:1 + SIG_LEN]
        cols = [tuple(body[i * 4:i * 4 + 4]) for i in range(COLS)]
        phase = body[56]
        clock = body[57]
        bufno = body[58]
        render = body[59]
        now = time.monotonic()
        d = self.diag

        if bufno not in (0, 1):
            self.say("BAD BUF", "signature reported buffer %02X" % bufno)

        self.cur_int = [c[0] for c in cols]

        if self.prev_cols is not None and d is not None and \
                d["is_on"] and d["bright"]:
            for c, (v, cr, cg, cb) in enumerate(cols):
                pv, pr, pg, pb = self.prev_cols[c]
                if pv >= 16 and v == 0 and self.prev_lit[c] >= 2:
                    self.counts["blackout"] += 1
                    self.say("BLACKOUT", "column %d went dark (was %d)  %s"
                             % (c, pv, self.describe()))
                # Between samples at 20 Hz the rainbow moves a few counts per
                # component.  Anything past 24 is a snap, not motion.
                if v >= 16 and pv >= 16:
                    j = max(abs(cr - pr), abs(cg - pg), abs(cb - pb))
                    if j > 24:
                        self.counts["colour"] += 1
                        self.say("COLOUR JUMP", "column %d %02X%02X%02X -> "
                                 "%02X%02X%02X (delta %d)  %s"
                                 % (c, pr, pg, pb, cr, cg, cb, j,
                                    self.describe()))
                        if self.args.trace:
                            print("   previous %s" % (self.prev_cols,),
                                  flush=True)

        # The phase must advance whenever frames advance.  A freeze while
        # led_frames keeps counting means the animation clock is not moving.
        if self.prev_phase is not None and self.prev_frames is not None \
                and d is not None:
            frames = (d["frames"] - self.prev_frames) & 0xFFFF
            if frames and phase == self.prev_phase:
                self.phase_stuck = getattr(self, "phase_stuck", 0) + 1
                if self.phase_stuck == 3:
                    self.counts["phase"] += 1
                    self.say("PHASE FROZEN", "phase unchanged over %d "
                             "samples while %d frames were published  %s"
                             % (self.phase_stuck, frames, self.describe()))
            else:
                self.phase_stuck = 0

        self.prev_cols = cols
        self.prev_lit = [1 if c[0] >= 8 else 0 for c in cols]
        self.prev_phase = phase
        self.prev_frames = d["frames"] if d else None
        self.sig = (phase, clock, bufno, render)

        if now >= self.next_print:
            self.next_print = now + self.args.print_every
            self.say("LIVE", "fps=%4.1f phase=%3d render=%2dms [%s] %s"
                     % (self.fps, phase, render, self.bars(),
                        EFFECT_NAMES.get(d["effect"], "?") if d else "?"))
            if self.args.trace and self.prev_cols:
                self.say("TRACE", " ".join("%02X%02X%02X" % c[1:]
                                           for c in self.prev_cols))

    # -- main loop -----------------------------------------------------
    def run(self):
        end = None if self.args.seconds <= 0 else \
            time.monotonic() + self.args.seconds
        diag_every = 1.0 / self.args.diag_hz
        sig_every = 1.0 / self.args.hz
        next_diag = time.monotonic()
        next_sig = time.monotonic()
        self.prev_lit = [0] * COLS
        self.prev_frames = None
        self.last_render = 0.0

        while running:
            now = time.monotonic()
            if end is not None and now >= end:
                break
            if now >= next_diag:
                next_diag = now + diag_every
                t = time.monotonic()
                r = self.dev.request([CMD_DIAG])
                self.requests += 1
                if r is None:
                    self.lost += 1
                else:
                    self.latencies.append(time.monotonic() - t)
                    d = self.parse_diag(r)
                    if d:
                        self.diag = d
                        self.check_state(d)
                        self.check_frames(d)
                        self.check_dma(d)
                        self.check_hold(d)
                        self.check_render(d)
            if time.monotonic() >= next_sig:
                next_sig = time.monotonic() + sig_every
                t = time.monotonic()
                r = self.dev.request([CMD_SIG])
                self.requests += 1
                if r is None:
                    self.lost += 1
                else:
                    self.latencies.append(time.monotonic() - t)
                    self.check_sig(r)
            time.sleep(0.002)

    def parse_diag(self, r):
        if len(r) < 1 + DIAG_LEN:
            return None
        d = dict(zip(NAMES, r[1:1 + DIAG_LEN]))
        d["frames"] = d["frames_lo"] | (d["frames_hi"] << 8)
        d["t"] = d["t_lo"] | (d["t_hi"] << 8)
        d["dma"] = d["dma_lo"] | (d["dma_hi"] << 8)
        return d

    def summary(self):
        self.say("SUMMARY", "%d requests, %d replies lost/stolen (%.1f%%)"
                 % (self.requests, self.lost,
                    100.0 * self.lost / max(1, self.requests)))
        if self.latencies:
            s = sorted(self.latencies)
            self.say("SUMMARY", "reply latency median %.1f ms, 95th %.1f ms, "
                     "max %.1f ms" % (s[len(s) // 2] * 1e3,
                                      s[int(len(s) * 0.95)] * 1e3,
                                      s[-1] * 1e3))
        if self.diag:
            self.say("SUMMARY", "final state: %s" % self.describe())
        if self.dma_seen:
            self.say("SUMMARY", "frame bases %s"
                     % ", ".join("%04X x%d" % (a, n)
                                 for a, n in sorted(self.dma_seen.items())))
        self.say("SUMMARY", "blackouts %d, phase jumps %d, colour jumps %d, "
                 "stalls %d, bad dma %d, slow renders %d, direct holds %d, "
                 "state changes %d"
                 % (self.counts["blackout"], self.counts["phase"],
                    self.counts["colour"], self.counts["stall"],
                    self.counts["dma"], self.counts["render"],
                    self.counts["hold"], self.counts["changes"]))


def on_signal(signum, frame):
    global running
    running = False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--path", help="hidraw node of the raw HID interface")
    ap.add_argument("--seconds", type=float, default=0,
                    help="stop after N seconds (0 = until Ctrl-C)")
    ap.add_argument("--hz", type=float, default=20, help="frame snapshot rate")
    ap.add_argument("--diag-hz", type=float, default=5, help="counter rate")
    ap.add_argument("--print-every", type=float, default=0.5,
                    help="seconds between LIVE lines")
    ap.add_argument("--trace", action="store_true",
                    help="also print the RGB of every column")
    args = ap.parse_args()

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    lib = load_hidapi()
    paths = [args.path] if args.path else find_paths(lib)
    if not paths:
        raise SystemExit("no raw HID interface (usage page 0xFF60) found")

    dev = None
    for p in paths:
        try:
            dev = Dev(lib, p)
            print("capturing on %s" % p, flush=True)
            break
        except OSError as e:
            print("  %s: %s" % (p, e), flush=True)
    if dev is None:
        raise SystemExit("cannot open the raw HID interface")

    cap = Capture(dev, args)
    try:
        cap.run()
    finally:
        cap.summary()
        dev.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
