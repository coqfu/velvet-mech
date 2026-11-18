"""Host-side sound-reactive lighting.

The CH555 has no microphone and no audio input, so nothing here runs on the
keyboard.  This module captures the monitor of the machine's default output
with `parec`, turns it into 14 logarithmic bands plus a level, and renders one
colour per LED.  The server streams those frames over the OpenRGB LED path,
which is the only path fast enough (measured ~180 frames/s on this board).

Everything in this file is honest about that split: the modes below are
implemented here, not in firmware, and the board sits in Direct while one is
running.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
import threading
import time

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BANDS = 14                 # one per matrix column
RATE = 48000
BLOCK = 1024               # frames per analysis window (~21 ms)
FFT = 2048                 # zero-padded transform for finer band edges
TARGET_FPS = 45

MODES = {
    "spectrum": "Column bands, one hue per frequency range.",
    "pulse": "Whole panel follows the low end, flashes on a kick.",
    "wave": "Band energy travels diagonally across the board.",
    "meter": "A level bar fills the board from the bottom left.",
    "spark": "Transients throw bright sparks at random keys.",
    "ripple": "Each kick sends a ring out from the centre.",
    "comet": "A bright head sweeps the rows, leaving a tail.",
}

PALETTES = {
    "prism": "Full spectrum, one hue per band.",
    "mono": "White, brightness only.",
    "heat": "Red through orange to pale yellow.",
    "ocean": "Deep blue with a teal crest.",
    "violet": "Violet to pink.",
    "sunset": "Magenta into orange.",
}


def _run(cmd, timeout=4):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return out.stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def _sources():
    """[(name, state)] from pactl, or [] when there is no sound server."""
    rows = []
    for line in _run(["pactl", "list", "short", "sources"]).splitlines():
        parts = line.split("\t")
        if len(parts) >= 5:
            rows.append((parts[1], parts[4]))
    return rows


def monitor_source():
    """The monitor of the default sink, else any monitor that is running.

    Recording the monitor is what makes system sound audible to this process
    without a loopback cable.  A sink with no monitor (some Bluetooth paths)
    falls back to another running monitor so the page still works.
    """
    rows = _sources()
    if not rows:
        return None, "no sound server (pactl found no sources)"
    default = _run(["pactl", "get-default-sink"]).strip()
    want = f"{default}.monitor" if default else None
    for name, _state in rows:
        if want and name == want:
            return name, None
    running = [n for n, s in rows if n.endswith(".monitor") and s.upper() == "RUNNING"]
    if running:
        return running[0], None
    any_monitor = [n for n, _ in rows if n.endswith(".monitor")]
    if any_monitor:
        return any_monitor[0], None
    return None, "the sound server exposes no monitor source"


def key_grid():
    """[(led, col, row)] for the 63 key LEDs, in LED order.

    host/map.json lists the keys in matrix order, which is also the LED order
    the firmware uses, so the list index is the LED index.
    """
    path = os.path.join(ROOT, "host", "map.json")
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return [(i, k["col"], k["row"]) for i, k in enumerate(data["keys"])]


GRID = key_grid()


# --------------------------------------------------------------- analysis --
def _db(x):
    return 20.0 * math.log10(max(x, 1e-9))


class Analyzer:
    """Log bands plus a level, with separate attack and release rates."""

    def __init__(self):
        self.bands = np.zeros(BANDS, dtype=np.float32)
        self.level = 0.0
        self.beat = 0.0
        self._last_beat = 0.0
        self._prev_low = 0.0
        freqs = np.fft.rfftfreq(FFT, 1.0 / RATE)
        edges = np.geomspace(45.0, 16000.0, BANDS + 1)
        self.slices = []
        for lo, hi in zip(edges[:-1], edges[1:]):
            idx = np.where((freqs >= lo) & (freqs < hi))[0]
            if len(idx) == 0:
                idx = np.array([min(len(freqs) - 1,
                                    int(np.searchsorted(freqs, lo)))])
            self.slices.append(idx)
        self.window = np.hanning(BLOCK).astype(np.float32)
        # A Hann-windowed full-scale sine peaks at sum(window)/2, so this is
        # the reference that turns a band maximum into dBFS.
        self.ref = float(self.window.sum()) / 2.0

    def feed(self, mono, gain=50.0):
        """mono: float32 -1..1 of exactly BLOCK frames."""
        # Zero-padding only refines the band edges; the window still sets the
        # real resolution, so latency stays at one BLOCK.
        spec = np.abs(np.fft.rfft(mono * self.window, n=FFT))
        raw = np.array([spec[idx].max() for idx in self.slices], dtype=np.float32)

        # Higher gain lowers the floor, which is the intuitive direction.
        floor = -70.0 - (gain - 50.0) * 0.45
        span = 42.0
        vals = np.clip((20.0 * np.log10(raw / self.ref + 1e-9) - floor) / span, 0.0, 1.0)
        vals = vals ** 1.6

        attack, release = 0.55, 0.10
        for i, v in enumerate(vals):
            k = attack if v > self.bands[i] else release
            self.bands[i] += (v - self.bands[i]) * k

        rms = float(np.sqrt(np.mean(mono * mono)))
        lvl = min(1.0, max(0.0, (_db(rms) + 56.0 + (gain - 50.0) * 0.30) / 46.0))
        self.level += (lvl - self.level) * (0.60 if lvl > self.level else 0.12)

        # Beat = a rising low band, not a loud one.  A sustained bass note
        # therefore does not keep retriggering the flash.
        low = float(self.bands[:3].mean())
        rise = low - self._prev_low
        self._prev_low += (low - self._prev_low) * 0.35
        now = time.time()
        if rise > 0.06 and low > 0.22 and now - self._last_beat > 0.14:
            self.beat = 1.0
            self._last_beat = now
        else:
            self.beat *= 0.90
        return self.bands, self.level, self.beat


# ---------------------------------------------------------------- rendering --
def _palette(name, t, v):
    """RGB 0..1 for a palette at position t (0..1) and intensity v (0..1)."""
    v = max(0.0, min(1.0, v))
    if name == "mono":
        return v, v, v
    if name == "heat":
        return v, v ** 2.2, v ** 4.0
    if name == "ocean":
        return 0.10 * v ** 2, 0.75 * v ** 1.4, v
    if name == "violet":
        return 0.72 * v, 0.18 * v ** 2, v
    if name == "sunset":
        return v, 0.52 * v ** 1.4, 0.30 * v ** 2.2
    # prism: colourize by position, keep the brightness from the band
    h = (t % 1.0) * 6.0
    i = int(h)
    f = h - i
    if i == 0:
        r, g, b = 1.0, f, 0.0
    elif i == 1:
        r, g, b = 1.0 - f, 1.0, 0.0
    elif i == 2:
        r, g, b = 0.0, 1.0, f
    elif i == 3:
        r, g, b = 0.0, 1.0 - f, 1.0
    elif i == 4:
        r, g, b = f, 0.0, 1.0
    else:
        r, g, b = 1.0, 0.0, 1.0 - f
    return r * v, g * v, b * v


def render(mode, palette, bands, level, beat, rear, frame):
    """One 69-LED frame: 63 keys in matrix order, then 6 rear LEDs."""
    ceiling = rear["ceiling"] / 255.0
    colours = []
    for led, col, row in GRID:
        if mode == "spectrum":
            t = col / BANDS
            v = float(bands[col % BANDS])
        elif mode == "wave":
            slot = (col + (4 - row) * 1.6 + frame * 0.45) % BANDS
            t = slot / BANDS
            v = float(bands[int(slot) % BANDS])
        elif mode == "meter":
            pos = (4 - row) * 14 + col
            fill = level * 74.0
            v = 1.0 if pos < fill else 0.0
            t = min(1.0, pos / 74.0)
        elif mode == "spark":
            seed = ((led * 2654435761) ^ (frame * 40503)) & 0xFFFFFFFF
            hot = beat > 0.55 and (seed % 97) < 9
            t = (seed % 255) / 255.0
            v = 1.0 if hot else level * 0.30
        elif mode == "ripple":
            # beat decays 1 -> 0, so the ring travels outward once per kick.
            radius = (1.0 - beat) * 7.5
            d = math.hypot(col - 6.5, (row - 2.0) * 2.2)
            ring = math.exp(-((d - radius) ** 2) / 0.75) * beat
            t = 0.55 + radius * 0.04
            v = min(1.0, level * 0.14 + ring)
        elif mode == "comet":
            span = 16.0
            pos = (frame * (0.35 + level * 3.2)) % span
            d = abs((col + 0.5) - (pos % span))
            d = min(d, span - d)
            tail = max(0.0, 1.0 - d / (2.5 + level * 5.0))
            t = (pos / span + 0.65) % 1.0
            v = min(1.0, level * 0.12 + tail ** 1.6)
        else:  # pulse
            t = 0.04 + level * 0.18
            v = min(1.0, level * 0.92 + beat * 0.35)
        r, g, b = _palette(palette, t, v)
        colours.append([
            int(min(255, r * 255 * ceiling)),
            int(min(255, g * 255 * ceiling)),
            int(min(255, b * 255 * ceiling)),
        ])

    if rear["on"]:
        # The board applies led_back_level to whatever colour it is given, so
        # the master rear level must not be folded in here as well: doing both
        # squared it (level 64 came out at a quarter instead of a half).  This
        # factor is the audio modulation only.
        v = min(1.0, 0.25 + level * 0.75 + beat * 0.15)
        base = rear["rgb"]
        # The firmware stores colours as bytes and set_colours() masks with
        # 0xFF.  Without this clamp a 255 component times v > 1 reached 293 and
        # wrapped to 37, so the strip flicked from white to a dim colour on the
        # loudest beats.
        colours.extend([[min(255, int(c * v)) for c in base] for _ in range(6)])
    else:
        colours.extend([[0, 0, 0] for _ in range(6)])
    return colours


# ------------------------------------------------------------------- thread --
class Engine:
    def __init__(self, send_frame, snapshot, rear):
        self.send_frame = send_frame
        self.snapshot = snapshot
        self.rear = rear
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.thread = None
        self.proc = None

        self.source, self.source_error = monitor_source()
        self.running = False
        self.mode = "spectrum"
        self.palette = "prism"
        self.gain = 50.0
        self.level = 0.0
        self.bands = [0.0] * BANDS
        self.beat = 0.0
        self.fps = 0.0
        self.error = None
        self.started = None

    def start(self, source=None):
        if self.running:
            return True
        if source:
            self.source = source
        if not self.source:
            self.error = self.source_error or "no monitor source"
            return False
        cmd = [
            "parec",
            f"--device={self.source}",
            f"--rate={RATE}",
            "--channels=2",
            "--format=s16le",
            "--latency-msec=20",
            "--raw",
        ]
        try:
            self.proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                bufsize=BLOCK * 4,
            )
        except OSError as exc:
            self.error = f"could not start parec: {exc}"
            return False
        self.stop.clear()
        self.running = True
        self.error = None
        self.started = time.time()
        self.thread = threading.Thread(target=self._loop, name="audio", daemon=True)
        self.thread.start()
        return True

    def stop_engine(self):
        self.stop.set()
        proc = self.proc
        self.proc = None
        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                proc.kill()
        self.running = False

    def _loop(self):
        chunk = BLOCK * 4          # frames * 2 channels * 2 bytes
        analyser = Analyzer()
        buf = b""
        frame_no = 0
        last = time.time()

        while not self.stop.is_set():
            loop_start = time.time()
            if not self.proc or self.proc.poll() is not None:
                self.error = "the audio capture stopped"
                break
            data = self.proc.stdout.read(chunk)
            if not data:
                self.error = "the audio capture returned no data"
                break
            buf += data
            # Keep the newest window; a slow frame must not build latency.
            if len(buf) > chunk:
                buf = buf[-chunk:]
            if len(buf) < chunk:
                continue
            pcm = np.frombuffer(buf, dtype="<i2").reshape(-1, 2)
            mono = pcm.astype(np.float32).mean(axis=1) / 32768.0
            buf = b""

            bands, level, beat = analyser.feed(mono, self.gain)
            frame_no += 1
            try:
                self.send_frame(render(self.mode, self.palette, bands, level,
                                       beat, self.rear, frame_no))
            except Exception as exc:                       # noqa: BLE001
                self.error = f"the keyboard stopped accepting frames: {exc}"
                break

            with self.lock:
                self.bands = [round(float(v), 4) for v in bands]
                self.level = round(float(level), 4)
                self.beat = round(float(beat), 4)
            now = time.time()
            self.fps = round(1.0 / max(1e-4, now - last), 1) if frame_no > 4 else 0.0
            last = now

            # Pace from the start of the loop.  The capture block itself is
            # ~21 ms, so the target is a ceiling rather than a promise.
            wait = 1.0 / TARGET_FPS - (time.time() - loop_start)
            if wait > 0:
                self.stop.wait(wait)

        self.running = False
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
        self.proc = None

    def state(self):
        with self.lock:
            return {
                "running": self.running,
                "mode": self.mode,
                "palette": self.palette,
                "gain": int(self.gain),
                "source": self.source,
                "error": self.error,
                "level": self.level,
                "bands": list(self.bands),
                "beat": self.beat,
                "fps": self.fps,
                "modes": [{"id": k, "desc": v} for k, v in MODES.items()],
                "palettes": [{"id": k, "desc": v} for k, v in PALETTES.items()],
                "targetFps": TARGET_FPS,
            }
