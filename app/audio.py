"""Host-side sound-reactive lighting.

The CH555 has no microphone and no audio input, so nothing here runs on the
keyboard.  This module captures what the machine is playing - the monitor of
the default output with `parec` on Linux, WASAPI loopback through SoundCard on
Windows - turns it into 14 logarithmic bands plus a level, and renders one
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
import sys
import threading
import time

import numpy as np

def _root():
    """Directory holding host/map.json.

    In a frozen build __file__ points inside the PYZ, so the two-level walk
    would land in %TEMP%; the bundle root (sys._MEIPASS) is the right answer
    there.  server.py puts the map at <bundle>/host/map.json for both.
    """
    if getattr(sys, "frozen", False):
        return getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


ROOT = _root()

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


# Which capture implementation this platform uses.  Linux keeps `parec`
# because that path is verified on the board; Windows (and macOS) go through
# SoundCard, whose WASAPI backend records an output device (loopback) without
# a virtual cable.  The env override lets the SoundCard path be exercised on
# Linux during development.
BACKEND = ("soundcard"
           if os.name == "nt" or sys.platform == "darwin"
           or os.environ.get("KEYFORGE_CAPTURE") == "soundcard"
           else "parec")

LOOPBACK_ID = "System output (loopback)"

# A console-less Windows build must not flash a console window for every
# helper process it starts.
NO_WINDOW = 0x08000000 if os.name == "nt" else 0


def _run(cmd, timeout=4):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                             creationflags=NO_WINDOW)
        return out.stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def _sources():
    """[(name, state)] the UI can pick from."""
    if BACKEND == "soundcard":
        return [(LOOPBACK_ID, "RUNNING")]
    rows = []
    for line in _run(["pactl", "list", "short", "sources"]).splitlines():
        parts = line.split("\t")
        if len(parts) >= 5:
            rows.append((parts[1], parts[4]))
    return rows


def monitor_source():
    """The output monitor this process will record, or (None, reason).

    Recording the monitor is what makes system sound audible to this process
    without a loopback cable.  A sink with no monitor (some Bluetooth paths)
    falls back to another running monitor so the page still works.  On Windows
    there is one entry: WASAPI loopback of the default speaker.
    """
    if BACKEND == "soundcard":
        return LOOPBACK_ID, None
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


_GRID = None


def grid():
    """key_grid(), read once on first use.

    Deliberately lazy: an incomplete bundle must not kill the whole service at
    import time (in a windowed build that is a modal dialog and looks like a
    hang).  server.py's --selftest checks host/map.json up front instead, so a
    missing map is reported as a broken bundle rather than as the app silently
    doing nothing.
    """
    global _GRID
    if _GRID is None:
        _GRID = key_grid()
    return _GRID


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
    for led, col, row in grid():
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
class Capture:
    """A byte stream of s16le stereo frames taken from the system output."""

    error = None

    def read(self, n):
        raise NotImplementedError

    def alive(self):
        return True

    def close(self):
        pass


class ParecCapture(Capture):
    """Linux: record a PulseAudio/PipeWire monitor with parec."""

    def __init__(self, source):
        cmd = [
            "parec",
            f"--device={source}",
            f"--rate={RATE}",
            "--channels=2",
            "--format=s16le",
            "--latency-msec=20",
            "--raw",
        ]
        try:
            self.proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                bufsize=BLOCK * 4, creationflags=NO_WINDOW,
            )
        except OSError as exc:
            raise RuntimeError(f"could not start parec: {exc}") from exc

    def read(self, n):
        return self.proc.stdout.read(n)

    def alive(self):
        return self.proc.poll() is None

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


class SoundcardCapture(Capture):
    """Windows: WASAPI loopback of the default speaker, through SoundCard.

    WASAPI can open an output device as a recorder, so no virtual cable is
    needed.  SoundCard is imported here rather than at module level so the
    Linux parec path does not need it installed.
    """

    def __init__(self, samplerate=RATE, blocksize=BLOCK):
        try:
            import soundcard as sc
        except ImportError as exc:
            raise RuntimeError(
                "sound capture on this platform needs the 'soundcard' package "
                "(pip install soundcard)") from exc

        self._sc = sc
        self._buf = bytearray()
        self._cond = threading.Condition()
        self._stop = threading.Event()
        self._rec = None
        self._thread = None
        self.name = "loopback"

        mic = None
        try:
            speaker = sc.default_speaker()
        except Exception:                                        # noqa: BLE001
            speaker = None
        if speaker is not None:
            try:
                # On Windows the speaker's name doubles as the loopback id.
                mic = sc.get_microphone(str(speaker.name), include_loopback=True)
            except Exception:                                    # noqa: BLE001
                mic = None
        if mic is None:
            try:
                loops = list(sc.all_microphones(include_loopback=True))
            except Exception:                                    # noqa: BLE001
                loops = []
            if loops:
                mic = loops[0]
        if mic is None and os.name != "nt":
            # Development path: on Linux the same class is exercised through
            # PulseAudio, where a speaker is not a recorder.
            try:
                mic = sc.default_microphone()
            except Exception:                                    # noqa: BLE001
                mic = None
        if mic is None:
            raise RuntimeError(
                "no loopback device: Windows exposes one per output, so check "
                "that a speaker or headphone is connected and selected")

        self.name = str(getattr(mic, "name", "loopback"))
        try:
            try:
                # SoundCard 0.5 can hand back int16 directly.
                rec = mic.recorder(samplerate=samplerate, channels=2,
                                   blocksize=blocksize, data_type="int16")
            except TypeError:
                # 0.4.x has no data_type and returns float32; _pump converts.
                rec = mic.recorder(samplerate=samplerate, channels=2,
                                   blocksize=blocksize)
            rec.__enter__()
        except Exception as exc:                                 # noqa: BLE001
            raise RuntimeError(f"could not open {self.name}: {exc}") from exc
        self._rec = rec
        self._thread = threading.Thread(target=self._pump, name="capture-pump",
                                        daemon=True)
        self._thread.start()

    def _pump(self):
        while not self._stop.is_set():
            try:
                data = self._rec.record(numframes=BLOCK)
            except Exception as exc:                             # noqa: BLE001
                self.error = f"capture failed: {exc}"
                break
            if data is None:
                continue
            raw = np.asarray(data)
            if raw.dtype != np.int16:
                raw = (np.clip(raw, -1.0, 1.0) * 32767.0).astype("<i2")
            with self._cond:
                self._buf += raw.tobytes()
                # A stalled consumer must not accumulate latency.
                limit = BLOCK * 4 * 12
                if len(self._buf) > limit:
                    del self._buf[:len(self._buf) - limit]
                self._cond.notify_all()
        with self._cond:
            self._cond.notify_all()

    def read(self, n):
        deadline = time.time() + 1.5
        with self._cond:
            while len(self._buf) < n:
                if self._stop.is_set() or not self.alive():
                    return b""
                left = deadline - time.time()
                if left <= 0:
                    return b""
                self._cond.wait(left)
            data = bytes(self._buf[:n])
            del self._buf[:n]
            return data

    def alive(self):
        return (not self._stop.is_set()
                and self._thread is not None and self._thread.is_alive())

    def close(self):
        self._stop.set()
        with self._cond:
            self._cond.notify_all()
        # The pump may be inside record(); free the stream only after it has
        # returned, otherwise libpulse is left with a freed stream object.
        if self._thread:
            self._thread.join(timeout=1.0)
        rec, self._rec = self._rec, None
        if rec is not None:
            try:
                rec.__exit__(None, None, None)
            except Exception:                                    # noqa: BLE001
                pass


def open_capture(source):
    if BACKEND == "soundcard":
        return SoundcardCapture()
    return ParecCapture(source)


class Engine:
    def __init__(self, send_frame, snapshot, rear):
        self.send_frame = send_frame
        self.snapshot = snapshot
        self.rear = rear
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.thread = None
        self.capture = None

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
        try:
            self.capture = open_capture(self.source)
        except Exception as exc:                                  # noqa: BLE001
            self.error = str(exc)
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
        capture = self.capture
        self.capture = None
        if capture:
            capture.close()
        self.running = False

    def _loop(self):
        chunk = BLOCK * 4          # frames * 2 channels * 2 bytes
        analyser = Analyzer()
        capture = self.capture
        buf = b""
        frame_no = 0
        last = time.time()

        while not self.stop.is_set():
            loop_start = time.time()
            if not capture or not capture.alive():
                self.error = capture.error if capture and capture.error \
                    else "the audio capture stopped"
                break
            data = capture.read(chunk)
            if not data:
                self.error = capture.error if capture.error \
                    else "the audio capture returned no data"
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
        if capture:
            capture.close()
        self.capture = None

    def state(self):
        with self.lock:
            return {
                "running": self.running,
                "mode": self.mode,
                "palette": self.palette,
                "gain": int(self.gain),
                "source": self.source,
                "backend": BACKEND,
                "error": self.error,
                "level": self.level,
                "bands": list(self.bands),
                "beat": self.beat,
                "fps": self.fps,
                "modes": [{"id": k, "desc": v} for k, v in MODES.items()],
                "palettes": [{"id": k, "desc": v} for k, v in PALETTES.items()],
                "targetFps": TARGET_FPS,
            }
