#!/usr/bin/env python3
"""KeyForge service - local bridge between the KeyForge UI and the keyboard.

The UI is a static React build (app/ui/dist).  This process serves it and
exposes a small JSON API over 127.0.0.1 that speaks the keyboard's OpenRGB
raw-HID protocol through libhidapi:

    python3 app/server.py            # http://127.0.0.1:8765/
    python3 app/server.py --dev      # UI from the Vite dev server instead

Hardware access stays here, in one place; the browser never touches USB.
"""

import argparse
import ctypes
import glob
import json
import mimetypes
import os
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FROZEN = bool(getattr(sys, "frozen", False))
BUNDLE = getattr(sys, "_MEIPASS", None)


def _app_root():
    """Directory holding ui/dist and host/map.json (the app package)."""
    if FROZEN:
        return os.path.join(BUNDLE or os.path.dirname(sys.executable), "app")
    return os.path.dirname(os.path.abspath(__file__))


ROOT = _app_root()
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)


def _data_dir():
    """Writable directory for profiles.json and the log.

    A frozen build unpacks to a temporary directory and an installed tree may
    be read-only, so the store lives with the user's data instead.  A source
    checkout keeps it in app/ so existing profiles keep working.
    """
    if not FROZEN and os.path.isdir(ROOT) and os.access(ROOT, os.W_OK):
        return ROOT
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
        path = os.path.join(base, "KeyForge")
    else:
        base = os.environ.get("XDG_DATA_HOME") or os.path.join(
            os.path.expanduser("~"), ".local", "share")
        path = os.path.join(base, "keyforge")
    try:
        os.makedirs(path, exist_ok=True)
    except OSError:
        return ROOT
    return path


DATA_DIR = _data_dir()
LOG_PATH = os.path.join(DATA_DIR, "keyforge.log")

# A console-less Windows build must not flash a console window for every
# helper process it starts.
NO_WINDOW = 0x08000000 if os.name == "nt" else 0

import audio  # noqa: E402  host-side sound capture, app/audio.py
DIST = os.path.join(ROOT, "ui", "dist")
PROFILES = os.path.join(DATA_DIR, "profiles.json")


def log(message):
    """One line in keyforge.log; the windowed build has no console."""
    line = "%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), message)
    try:
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            f.write(line)
    except OSError:
        pass

# --------------------------------------------------------------- protocol --
PACKET = 65
CMD_PROTOCOL = 1
CMD_QMK_VERSION = 2
CMD_DEVICE_INFO = 3
CMD_MODE_INFO = 4
CMD_LED_INFO = 5
CMD_ENABLED_MODES = 6
CMD_SET_MODE = 7
CMD_SET_LEDS = 9
CMD_SET_STATE = 0x70
CMD_GET_STATE = 0x71
CMD_GET_REMAP = 0x72
CMD_SET_REMAP = 0x73
CMD_GET_CUSTOM = 0x74
CMD_SET_CUSTOM = 0x75
CMD_SAVE_ALL = 0x76
CMD_GET_MACRO = 0x77
CMD_SET_MACRO = 0x78
CMD_SET_CONFIG = 0x79
CMD_BOOTLOADER = 0x7A
CMD_BOOT_PREVIEW = 0x7B
CMD_DIAG = 0x7E

MACRO_SLOTS = 4
MACRO_STEPS = 24
MACRO_UNBOUND = 0xFF

# Firmware effect ids, and the mode number the wire protocol uses.  OpenRGB
# numbers modes by their position in the list the board advertises (Direct is
# assigned last, then shown first), not by their QMK id, so the board's parser
# speaks these positions too.  Modes 1..5 are Static, Breathing, Cycle All, Hue
# Wave and Solid Reactive; Direct is 6; everything else is 100 + effect id.
DIRECT_MODE = 6
EFFECT_WIRE = {0: 100, 1: 1, 2: 2, 3: 3, 4: 104, 5: 4, 6: 5,
               7: 107, 8: 108, 9: 109, 10: 110, 11: 111, 12: 112,
               13: 113, 14: 114, 15: 115, 16: 116, 17: 117, 18: 118,
               19: 119, 20: 120, 21: 121, 22: 122, 23: 123, 24: 124,
               25: 125, 26: 126, 27: 127, 28: 128, 29: 129, 30: 130,
               31: 131, 32: 132, 33: 133, 34: 134}
WIRE_EFFECT = {v: k for k, v in EFFECT_WIRE.items()}

LED_MODE_EXTRA = 100
EFFECT_COUNT = 35          # LED_FX_USER_COUNT: OFF..CUSTOM

KEY_COUNT = 63
UNDERGLOW_COUNT = 6
LED_COUNT = KEY_COUNT + UNDERGLOW_COUNT
FRAME_MS = 11.2


def _hidapi():
    if os.name == "nt":
        names = ("hidapi.dll", "libhidapi-0.dll", "libhidapi.dll")
    elif sys.platform == "darwin":
        names = ("libhidapi.dylib",
                 "/opt/homebrew/lib/libhidapi.dylib",
                 "/usr/local/lib/libhidapi.dylib")
    else:
        names = ("libhidapi-hidraw.so.0", "libhidapi-hidraw.so",
                 "libhidapi-libusb.so.0", "libhidapi-libusb.so")

    # A frozen build unpacks hidapi.dll beside the bundled modules, and the
    # loader does not search there on its own.
    here = os.path.dirname(os.path.abspath(sys.executable))
    search = [p for p in (BUNDLE, here, os.path.dirname(ROOT)) if p]
    last = None
    for name in names:
        for candidate in [name] + [os.path.join(d, name) for d in search]:
            try:
                return ctypes.CDLL(candidate)
            except OSError as exc:
                last = exc
    raise RuntimeError(
        "hidapi not found (%s). Windows: keep hidapi.dll next to "
        "KeyForge.exe. Linux: install hidapi." % last)


try:
    LIB = _hidapi()
    LIB_ERROR = None
except RuntimeError as exc:
    LIB = None
    LIB_ERROR = str(exc)

if LIB is not None:
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

    LIB.hid_enumerate.restype = ctypes.POINTER(Info)
    LIB.hid_enumerate.argtypes = [ctypes.c_ushort, ctypes.c_ushort]
    LIB.hid_free_enumeration.argtypes = [ctypes.POINTER(Info)]
    LIB.hid_free_enumeration.restype = None
    LIB.hid_open_path.restype = ctypes.c_void_p
    LIB.hid_open_path.argtypes = [ctypes.c_char_p]
    LIB.hid_close.argtypes = [ctypes.c_void_p]
    LIB.hid_close.restype = None
    LIB.hid_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
    LIB.hid_write.restype = ctypes.c_int
    LIB.hid_read_timeout.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]
    LIB.hid_read_timeout.restype = ctypes.c_int


def enumerate_raw():
    """(path, vendor, product) of the keyboard's raw-HID collection."""
    if LIB is None:
        return None
    head = LIB.hid_enumerate(0, 0)
    node = head
    found = None
    while node:
        i = node.contents
        if i.usage_page == 0xFF60 and i.usage == 0x61 and i.path:
            found = (i.path.decode(), i.vendor_id, i.product_id)
            break
        node = i.next
    LIB.hid_free_enumeration(head)
    return found


class Device:
    def __init__(self):
        # Re-entrant: every entry point takes this lock, and a request that
        # times out reopens the handle while it is already inside one.  A plain
        # Lock let a second HTTP thread close the handle underneath a read that
        # was still blocked in hid_read_timeout, which is a use-after-free in
        # hidapi (the app died, or the reply went to freed memory).
        self.lock = threading.RLock()
        self.handle = None
        self.path = None
        self.vendor_id = None
        self.product_id = None
        self.paused = False
        self.info = {}
        self.synced = False

    # -- lifecycle
    def open(self):
        with self.lock:
            if self.paused:
                return False
            found = enumerate_raw()
            if not found:
                self.close()
                return False
            path, vid, pid = found
            if self.handle and path == self.path:
                return True
            self.close()
            handle = LIB.hid_open_path(path.encode())
            if not handle:
                return False
            self.handle = handle
            self.path = path
            self.vendor_id = vid
            self.product_id = pid
            return True

    def close(self):
        with self.lock:
            if self.handle:
                LIB.hid_close(self.handle)
            self.handle = None
            self.path = None
            # A replugged board comes back with the settings it saved;
            # identity, remap and picture have to be read again before the app
            # trusts them.
            self.synced = False
            self.info = {}

    # -- io
    def reopen(self):
        """Drop the handle and look for the board again.

        A firmware restart or a replug renumbers the hidraw node while the old
        handle stays open and silent.  Without this the app keeps whatever it
        last read instead of noticing that the board came back on its saved
        settings.
        """
        if self.paused:
            return False
        with self.lock:
            self.close()
            return self.open()

    def drain(self):
        """Throw away replies nobody is waiting for.

        A timed-out command leaves its reply in the endpoint buffer.  The next
        command would then read *that* reply, see the wrong command echo and be
        reported as a rejection.  Flushing first keeps replies paired with
        their requests.
        """
        with self.lock:
            if not self.handle:
                return
            out = ctypes.create_string_buffer(PACKET)
            for _ in range(4):
                if LIB.hid_read_timeout(self.handle, out, PACKET, 0) <= 0:
                    return

    def request(self, data, timeout=400):
        """One command, one reply.

        A stale or truncated reply used to surface in the UI as "the keyboard
        rejected the mode packet", so a reply is retried when it carries the
        wrong command echo or does not arrive at all.
        """
        reply = None
        for attempt in range(3):
            reply = self._request_once(data, timeout)
            if reply and reply[0] == (data[0] & 0xFF):
                return reply
            # A dead handle looks exactly like a slow device: the read times
            # out.  Re-enumerate between attempts so a restart or replug is
            # picked up instead of leaving the app on stale values.
            if attempt < 2:
                self.reopen()
        return reply

    def _request_once(self, data, timeout):
        with self.lock:
            if not self.handle and not self.open():
                return None
            self.drain()
            buf = bytearray(PACKET)
            buf[1:1 + len(data)] = bytes(data)
            self._write(buf)
            out = ctypes.create_string_buffer(PACKET)
            n = LIB.hid_read_timeout(self.handle, out, PACKET, timeout)
            if n <= 0:
                # A reply that arrived after its deadline is still queued; drop
                # it rather than let the next command mistake it for its own.
                self._drain(4)
                return None
            return list(out.raw[:n])

    def _drain(self, limit):
        """Discard replies left over from a timed-out command."""
        out = ctypes.create_string_buffer(PACKET)
        for _ in range(limit):
            if LIB.hid_read_timeout(self.handle, out, PACKET, 1) <= 0:
                return

    def send(self, data):
        with self.lock:
            if not self.handle and not self.open():
                raise RuntimeError("keyboard not connected")
            buf = bytearray(PACKET)
            buf[1:1 + len(data)] = bytes(data)
            self._write(buf)

    def _write(self, buf):
        if LIB.hid_write(self.handle, bytes(buf), PACKET) < 0:
            self.close()
            raise RuntimeError("write failed - device gone")


DEV = Device()

# ------------------------------------------------------------------ state --
STATE = {
    "mode": 3,
    "effect": 3,
    "hue": 20,
    "sat": 255,
    "val": 160,
    "speed": 240,
    "colours": [[0, 0, 0] for _ in range(LED_COUNT)],
    "custom": [[0, 0, 0] for _ in range(KEY_COUNT)],
    "remap": [255] * KEY_COUNT,
    "macros": [{"key": 255, "len": 0, "steps": []} for _ in range(MACRO_SLOTS)],
    "backOn": 1,
    "backLevel": 127,
    "backRgb": [255, 255, 255],
    "cal": [128, 128, 128],
    "debounce": 2,
    "gaming": 0,
    "bootMode": 1,
    "bootPace": 1,
    "backMode": 0,
    "direct": False,
}
STATE_LOCK = threading.Lock()
LAST_DIAG = None
LAST_DIAG_AT = 0.0
LAST_LIVE_AT = 0.0
LAST_FRAMES = None
DIAG_FPS = None


def _ascii(data):
    """Printable text from one reply field.

    Replies are zero-padded and end with OpenRGB's 'd' terminator in the last
    byte, so take the longest printable run rather than every printable byte.
    """
    runs = _strings(data)
    return max(runs, key=len) if runs else ""


def _strings(data):
    """Every printable, non-empty run in a reply field, in order."""
    text = "".join(chr(c) if 32 <= c < 127 else "\x00" for c in data)
    return [run.strip() for run in text.split("\x00") if run.strip()]


def read_colours():
    out = []
    first = 0
    while first < LED_COUNT:
        n = min(8, LED_COUNT - first)
        r = DEV.request([CMD_LED_INFO, first, n])
        if not r or r[0] != CMD_LED_INFO:
            return None
        for k in range(n):
            o = 1 + k * 7
            out.append([r[o + 3], r[o + 4], r[o + 5]])
        first += n
    return out


def read_state():
    """Effect, colour, brightness, speed and every hardware trim.

    One packet, so the UI always sees a state the board was actually in.
    """
    r = DEV.request([CMD_GET_STATE])
    if not r or len(r) < 17 or r[0] != CMD_GET_STATE:
        return None
    return {
        "effect": r[1],
        "hue": r[2],
        "sat": r[3],
        "val": r[4],
        "speed": r[5],
        "backOn": r[6],
        "backLevel": r[7],
        "direct": bool(r[8]),
        "gaming": r[9],
        "debounce": r[10],
        "cal": [r[11], r[12], r[13]],
        "backRgb": [r[14], r[15], r[16]],
        # Present from firmware 5; older boards simply do not send them.
        "bootMode": r[17] if len(r) > 19 else 1,
        "bootPace": r[18] if len(r) > 19 else 1,
        "backMode": r[19] if len(r) > 19 else 0,
    }


def refresh_live_state(min_gap=0.5):
    """Re-read the registers the panel is actually showing.

    STATE used to be filled once per connection, so anything that moved the
    board behind the app's back - a Fn combo, a second client, a replug after
    a reflash - left the app displaying values the board did not hold, and the
    next APPLY wrote those stale values back.  One 0x71 packet per poll keeps
    the two in step; identity, remap, picture and macros are still read only
    on connect because they are large and change only from this app.
    """
    global LAST_LIVE_AT
    now = time.time()
    if now - LAST_LIVE_AT < min_gap:
        return
    # While the sound engine streams frames, the panel reports Direct and
    # whatever colour the last frame held.  That is a temporary override, not
    # the lighting state: copying it into STATE made /api/status show Direct
    # and let the UI adopt it, so an edit made during playback would later be
    # applied as a Direct frame instead of the effect the user configured.
    # The snapshot is restored (and STATE rewritten) when the engine stops.
    # A dead-but-not-yet-reaped engine still leaves the panel in Direct, so
    # the test is "an engine exists", not "its thread says running".
    if AUDIO_ENGINE:
        return
    LAST_LIVE_AT = now
    state = read_state()
    if not state:
        return
    with STATE_LOCK:
        STATE.update(
            effect=state["effect"],
            mode=DIRECT_MODE if state["direct"] else EFFECT_WIRE.get(state["effect"], 100 + state["effect"]),
            hue=state["hue"],
            sat=state["sat"],
            val=state["val"],
            speed=state["speed"],
            backOn=state["backOn"],
            backLevel=state["backLevel"],
            backRgb=state["backRgb"],
            cal=state["cal"],
            debounce=state["debounce"],
            gaming=state["gaming"],
            bootMode=state["bootMode"],
            bootPace=state["bootPace"],
            backMode=state["backMode"],
            direct=state["direct"],
        )


def read_remap():
    """One HID usage per key; 0xFF means the factory keycode."""
    out = [0xFF] * KEY_COUNT
    first = 0
    while first < KEY_COUNT:
        n = min(31, KEY_COUNT - first)
        r = DEV.request([CMD_GET_REMAP, first, n])
        if not r or r[0] != CMD_GET_REMAP:
            return None
        for k in range(n):
            idx = r[1 + k * 2]
            if idx < KEY_COUNT:
                out[idx] = r[2 + k * 2]
        first += n
    return out


def read_custom():
    """The per-key picture used by the Custom effect."""
    out = [[0, 0, 0] for _ in range(KEY_COUNT)]
    first = 0
    while first < KEY_COUNT:
        n = min(15, KEY_COUNT - first)
        r = DEV.request([CMD_GET_CUSTOM, first, n])
        if not r or r[0] != CMD_GET_CUSTOM:
            return None
        for k in range(n):
            o = 1 + k * 4
            idx = r[o]
            if idx < KEY_COUNT:
                out[idx] = [r[o + 1], r[o + 2], r[o + 3]]
        first += n
    return out


def read_macros():
    """The four macro slots, or None if the firmware does not answer."""
    out = []
    for slot in range(MACRO_SLOTS):
        r = DEV.request([CMD_GET_MACRO, slot])
        if not r or r[0] != CMD_GET_MACRO:
            return None
        key = r[1]
        length = r[2]
        if length > MACRO_STEPS:
            length = MACRO_STEPS
        steps = []
        for i in range(length):
            o = 3 + i * 2
            steps.append({"usage": r[o], "hold": r[o + 1]})
        out.append({"key": key, "len": length, "steps": steps})
    return out


def sync_from_device():
    """Read identity, the live effect state, the remap and the custom picture."""
    if not DEV.open():
        return False
    proto = DEV.request([CMD_PROTOCOL])
    qmk = DEV.request([CMD_QMK_VERSION])
    name = DEV.request([CMD_DEVICE_INFO])
    enabled = DEV.request([CMD_ENABLED_MODES])
    state = read_state()
    # The private state packet is new; while an older firmware is still on the
    # board the reply never comes, so fall back to the OpenRGB mode reply and
    # skip the remap / picture reads instead of stalling every page load.
    legacy_mode = None
    remap = custom = colours = macros = None
    if state is None:
        legacy_mode = DEV.request([CMD_MODE_INFO])
    else:
        colours = read_colours() if state["direct"] else None
        remap = read_remap()
        custom = read_custom()
        macros = read_macros()

    info_strings = _strings(name[3:]) if name else []
    DEV.info = {
        "name": info_strings[0] if info_strings else "Velvet Mech",
        "vendor": info_strings[1] if len(info_strings) > 1 else "velvet.mech",
        "qmk": _ascii(qmk[1:]) if qmk else "",
        "protocol": proto[1] if proto else None,
        # Enabled modes is a zero-terminated list; the reply terminator follows
        # it, and mode 0 is the terminator rather than a real mode.
        "modes": [m for m in (enabled[1:] if enabled else []) if 0 < m < 100][:16]
        or [1, 2, 6, 13, 28, 32],
    }
    with STATE_LOCK:
        if colours:
            STATE["colours"] = colours
        if remap:
            STATE["remap"] = remap
        if custom:
            STATE["custom"] = custom
        if macros:
            STATE["macros"] = macros
        if state:
            STATE.update(
                effect=state["effect"],
                mode=DIRECT_MODE if state["direct"] else EFFECT_WIRE.get(state["effect"], 100 + state["effect"]),
                hue=state["hue"],
                sat=state["sat"],
                val=state["val"],
                speed=state["speed"],
                backOn=state["backOn"],
                backLevel=state["backLevel"],
                backRgb=state["backRgb"],
                cal=state["cal"],
                debounce=state["debounce"],
                gaming=state["gaming"],
                bootMode=state.get("bootMode", STATE["bootMode"]),
                bootPace=state.get("bootPace", STATE["bootPace"]),
                backMode=state.get("backMode", STATE["backMode"]),
                direct=state["direct"],
            )
        elif legacy_mode and len(legacy_mode) >= 6:
            STATE.update(
                mode=legacy_mode[1],
                hue=legacy_mode[3],
                sat=legacy_mode[4],
                val=legacy_mode[5],
                speed=legacy_mode[2],
                direct=bool(legacy_mode[1] == 1),
            )
    DEV.synced = True
    return True


def device_payload(connected):
    info = DEV.info or {}
    return {
        "connected": bool(connected),
        "released": bool(DEV.paused),
        "path": DEV.path,
        "vendorId": DEV.vendor_id,
        "productId": DEV.product_id,
        "name": info.get("name") or "Velvet Mech",
        "vendor": info.get("vendor") or "velvet.mech",
        "qmk": info.get("qmk") or "",
        "protocol": info.get("protocol"),
        "keyCount": KEY_COUNT,
        "ledCount": LED_COUNT,
        "underglowCount": UNDERGLOW_COUNT,
        "modes": info.get("modes") or [1, 2, 6, 13, 28, 32],
    }


def lighting_payload():
    with STATE_LOCK:
        return {
            "mode": STATE["mode"],
            "effect": STATE["effect"],
            "hue": STATE["hue"],
            "sat": STATE["sat"],
            "val": STATE["val"],
            "speed": STATE["speed"],
            "colours": [list(c) for c in STATE["colours"]],
            "custom": [list(c) for c in STATE["custom"]],
            "backOn": STATE["backOn"],
            "backLevel": STATE["backLevel"],
            "backRgb": list(STATE["backRgb"]),
            "cal": list(STATE["cal"]),
            "debounce": STATE["debounce"],
            "gaming": STATE["gaming"],
            "bootMode": STATE["bootMode"],
            "bootPace": STATE["bootPace"],
            "backMode": STATE["backMode"],
            "direct": STATE["direct"],
            "remap": list(STATE["remap"]),
            "macros": [dict(m) for m in STATE["macros"]],
        }


RESET_CAUSE = {
    0x00: "software reset",
    0x10: "power-on",
    0x20: "watchdog",
    0x30: "reset pin",
}


def parse_diag(raw):
    """Reply payload is [cmd, ver, irqs, frames lo/hi, dma lo/hi, show, work,
    ready, on, back, bright, effect, speed, direct, have_colors, boot,
    t lo/hi, color_r, render_ms, phase, reset]."""
    if not raw or len(raw) < 23:
        return None
    o = raw[1:]
    frames = o[2] | (o[3] << 8)
    dma = o[4] | (o[5] << 8)
    return {
        "fps": DIAG_FPS,
        "renderMs": o[20],
        "frames": frames,
        "irqs": o[1],
        "dmaBase": dma,
        "directActive": bool(o[14]),
        "on": bool(o[9]),
        "backLight": bool(o[10]),
        "brightness": o[11],
        "effect": o[12],
        "speed": o[13],
        "phase": o[21],
        # Layout 1 stops at the phase byte; the reset cause arrived in
        # layout 2.  The reply is padded to the packet size, so the
        # version byte - not the length - says whether it is real.
        "reset": RESET_CAUSE.get(o[22] & 0x30) if o[0] >= 2 else None,
        # Layout 3 adds save health: completed save rounds, the ROM
        # controller's verdict and whether the block verified at boot.
        "writes": o[23] if o[0] >= 3 else None,
        "romStatus": o[24] if o[0] >= 3 else None,
        "blockValid": bool(o[25]) if o[0] >= 3 else None,
        # Layout 4 adds the save-path internals: which sections are queued,
        # the debounce countdown and the loop/change-detector counters.
        "saveFlags": o[26] if o[0] >= 4 else None,
        "saveTimer": (o[27] | (o[28] << 8)) if o[0] >= 4 else None,
        "saveRounds": o[29] if o[0] >= 4 else None,
        "shadowHits": o[30] if o[0] >= 4 else None,
        # Layout 5 adds the flash write shape the save path settled on.
        "saveShape": o[31] if o[0] >= 5 else None,
    }


def read_diag(force=False):
    global LAST_DIAG, LAST_DIAG_AT, LAST_FRAMES, DIAG_FPS
    now = time.time()
    if not force and LAST_DIAG and now - LAST_DIAG_AT < 0.5:
        return LAST_DIAG
    if not DEV.open():
        return None
    raw = DEV.request([CMD_DIAG])
    if not raw or len(raw) < 23:
        return LAST_DIAG
    frames = raw[3] | (raw[4] << 8)
    if LAST_FRAMES is not None:
        dt = now - LAST_DIAG_AT
        # A counter that jumped by more than half the 16-bit range did not
        # advance - the board restarted.  The wrap arithmetic would turn that
        # into a huge fake rate and one bogus dip in the trace, so drop the
        # sample and let the next two seconds set a fresh baseline.
        delta = (frames - LAST_FRAMES) & 0xFFFF
        if delta > 0x8000:
            DIAG_FPS = None
        elif dt > 0.05:
            sample = delta / dt
            DIAG_FPS = (round(sample) if DIAG_FPS is None
                        else round(DIAG_FPS * 0.7 + sample * 0.3))
    LAST_FRAMES = frames
    LAST_DIAG = parse_diag(raw)
    LAST_DIAG_AT = now
    return LAST_DIAG


def set_mode(lighting, save=False):
    """OpenRGB path, used for Direct.  Effects go through CMD_SET_STATE."""
    speed = int(lighting.get("speed", STATE["speed"])) & 0xFF
    hue = int(lighting.get("hue", 0)) & 0xFF
    sat = int(lighting.get("sat", 255)) & 0xFF
    val = int(lighting.get("val", 160)) & 0xFF
    mode = int(lighting.get("mode", DIRECT_MODE))
    # Bit 7 tells the firmware this value byte really is a dimmer.  OpenRGB
    # sends val = 0 when it selects Direct and never sets the bit, so its
    # selection cannot black the panel out; the app's Levels slider does.
    flags = (0x80 if mode == DIRECT_MODE else 0) | (1 if save else 0)
    r = DEV.request([CMD_SET_MODE, hue, sat, val, mode, speed, flags])
    return bool(r and len(r) > 1 and r[1] == 50)


def set_colours(colours):
    sent = 0
    while sent < len(colours):
        n = min(15, len(colours) - sent)
        payload = [CMD_SET_LEDS, n]
        for k in range(n):
            c = colours[sent + k]
            payload += [sent + k, int(c[0]) & 0xFF, int(c[1]) & 0xFF, int(c[2]) & 0xFF]
        DEV.send(payload)
        sent += n


def effect_for(lighting):
    """Firmware effect id for a lighting payload.

    Returns None for Direct, which is not an effect: it paints the LEDs
    through the OpenRGB LED path instead.  The UI may send the effect id
    directly or the wire mode number it was given, so both are accepted.
    """
    effect = lighting.get("effect")
    if effect is not None:
        effect = int(effect)
        if 0 <= effect < EFFECT_COUNT:
            return effect
    mode = int(lighting.get("mode", 3))
    if mode == DIRECT_MODE:
        return None
    if mode in WIRE_EFFECT:
        return WIRE_EFFECT[mode]
    if mode >= LED_MODE_EXTRA:
        effect = mode - LED_MODE_EXTRA
        if 0 <= effect < EFFECT_COUNT:
            return effect
    return 3


def apply_lighting(lighting, save=False):
    """Write one complete lighting state to the panel."""
    if not DEV.open():
        raise RuntimeError("keyboard not connected")

    effect = effect_for(lighting)
    hue = int(lighting.get("hue", STATE["hue"])) & 0xFF
    sat = int(lighting.get("sat", STATE["sat"])) & 0xFF
    val = int(lighting.get("val", STATE["val"])) & 0xFF
    speed = int(lighting.get("speed", STATE["speed"])) & 0xFF

    colours = lighting.get("colours")
    if isinstance(colours, list) and len(colours) == LED_COUNT:
        clean = [[int(c[0]) & 0xFF, int(c[1]) & 0xFF, int(c[2]) & 0xFF] for c in colours]
    else:
        clean = None

    if effect is None:
        if not set_mode({**lighting, "mode": DIRECT_MODE}, save=save):
            raise RuntimeError("the keyboard rejected the mode packet")
        if clean is not None:
            set_colours(clean)
    else:
        r = DEV.request([CMD_SET_STATE, effect, hue, sat, val, speed, 1 if save else 0])
        if not r or len(r) < 2 or r[1] != 50:
            raise RuntimeError("the keyboard rejected the state packet")

    with STATE_LOCK:
        if effect is None:
            STATE.update(mode=DIRECT_MODE, direct=True)
        else:
            STATE.update(
                effect=effect,
                mode=EFFECT_WIRE.get(effect, LED_MODE_EXTRA + effect),
                direct=False,
            )
        STATE.update(hue=hue, sat=sat, val=val, speed=speed)
        if clean is not None:
            STATE["colours"] = clean


def set_config(cfg):
    """Debounce, gaming, back light, calibration and the two animations."""
    with STATE_LOCK:
        debounce = int(cfg.get("debounce", STATE["debounce"]))
        gaming = int(bool(cfg.get("gaming", STATE["gaming"])))
        back_on = int(bool(cfg.get("backOn", STATE["backOn"])))
        back_level = int(cfg.get("backLevel", STATE["backLevel"]))
        cal = list(cfg.get("cal", STATE["cal"]))
        back_rgb = list(cfg.get("backRgb", STATE["backRgb"]))
        boot_mode = int(cfg.get("bootMode", STATE["bootMode"]))
        boot_pace = int(cfg.get("bootPace", STATE["bootPace"]))
        back_mode = int(cfg.get("backMode", STATE["backMode"]))

    debounce = max(1, min(6, debounce))
    back_level = max(0, min(127, back_level))
    cal = [max(0, min(255, int(v))) for v in (cal + [128, 128, 128])[:3]]
    back_rgb = [max(0, min(255, int(v))) for v in (back_rgb + [255, 255, 255])[:3]]
    boot_mode = max(0, min(5, boot_mode))
    boot_pace = max(0, min(2, boot_pace))
    back_mode = max(0, min(8, back_mode))

    if not DEV.open():
        raise RuntimeError("keyboard not connected")
    r = DEV.request(
        [CMD_SET_CONFIG, debounce, gaming, back_on, back_level,
         cal[0], cal[1], cal[2], back_rgb[0], back_rgb[1], back_rgb[2],
         boot_mode, boot_pace, back_mode]
    )
    if not r or len(r) < 2 or r[1] != 50:
        raise RuntimeError("the keyboard rejected the config packet")

    with STATE_LOCK:
        STATE.update(
            debounce=debounce,
            gaming=gaming,
            backOn=back_on,
            backLevel=back_level,
            cal=cal,
            backRgb=back_rgb,
            bootMode=boot_mode,
            bootPace=boot_pace,
            backMode=back_mode,
        )
    if AUDIO_ENGINE and AUDIO_ENGINE.running:
        AUDIO_ENGINE.rear = _audio_rear()
    return True


def boot_preview():
    """Replay the selected start-up animation on the board."""
    if not DEV.open():
        raise RuntimeError("keyboard not connected")
    r = DEV.request([CMD_BOOT_PREVIEW])
    if not r or len(r) < 2 or r[1] != 50:
        raise RuntimeError("the keyboard did not start the animation")


def set_remap(index, usage):
    if not DEV.open():
        raise RuntimeError("keyboard not connected")
    if not (0 <= int(index) < KEY_COUNT):
        raise RuntimeError("no such key")
    usage = int(usage) & 0xFF
    r = DEV.request([CMD_SET_REMAP, int(index), usage])
    if not r or len(r) < 2 or r[1] != 50:
        raise RuntimeError("the keyboard rejected the remap packet")
    with STATE_LOCK:
        STATE["remap"][int(index)] = usage


def set_remap_all(usages):
    if len(usages) != KEY_COUNT:
        raise RuntimeError("a remap needs one usage per key")
    # One packet per key, so a whole-table write is 63 round trips and a single
    # dropped reply used to abort the batch half-applied.  Retry each key on
    # its own before giving up.
    for i, usage in enumerate(usages):
        last = None
        for _ in range(3):
            try:
                set_remap(i, usage)
                last = None
                break
            except RuntimeError as exc:                      # noqa: PERF203
                last = exc
                time.sleep(0.03)
        if last is not None:
            raise RuntimeError(f"key {i}: {last}")


def set_custom(picture):
    """Send the custom picture in the 15-entry packets the firmware takes."""
    if not DEV.open():
        raise RuntimeError("keyboard not connected")
    if len(picture) != KEY_COUNT:
        raise RuntimeError("a custom picture needs one colour per key")
    clean = [[int(c[0]) & 0xFF, int(c[1]) & 0xFF, int(c[2]) & 0xFF] for c in picture]
    first = 0
    while first < KEY_COUNT:
        n = min(15, KEY_COUNT - first)
        payload = [CMD_SET_CUSTOM, n]
        for k in range(n):
            c = clean[first + k]
            payload += [first + k, c[0], c[1], c[2]]
        r = DEV.request(payload)
        if not r or len(r) < 2 or r[1] != 50:
            raise RuntimeError("the keyboard rejected the custom packet")
        first += n
    with STATE_LOCK:
        STATE["custom"] = clean


def _macro_step(step):
    """Normalise one recorded step to [usage, hold].

    The UI posts objects (``{"usage": 4, "hold": 3}``); older callers and the
    stored state use pairs, and a bare number means "tap this usage".  All
    three shapes reach the same packet.
    """
    if isinstance(step, dict):
        usage = step.get("usage", 0)
        hold = step.get("hold", 0)
    elif isinstance(step, (list, tuple)):
        usage = step[0] if len(step) > 0 else 0
        hold = step[1] if len(step) > 1 else 0
    else:
        usage, hold = step, 0
    return [int(usage) & 0xFF, int(hold) & 0xFF]


def set_macro(slot, key, steps):
    """Write one macro slot: a binding key plus up to 24 (usage, hold) steps."""
    slot = int(slot)
    if not (0 <= slot < MACRO_SLOTS):
        raise RuntimeError("no such macro slot")
    key = int(key) & 0xFF
    clean = [_macro_step(step) for step in steps[:MACRO_STEPS]]
    if clean and key == KEY_COUNT - 1:      # K_FN: a layer key, not a trigger
        raise RuntimeError("Fn cannot be a macro trigger")
    if not DEV.open():
        raise RuntimeError("keyboard not connected")
    payload = [CMD_SET_MACRO, slot, key, len(clean)]
    for usage, hold in clean:
        payload += [usage, hold]
    r = DEV.request(payload)
    if not r or len(r) < 2 or r[1] != 50:
        raise RuntimeError("the keyboard rejected the macro packet")
    with STATE_LOCK:
        STATE["macros"][slot] = {
            "key": key if key < KEY_COUNT else 0xFF,
            "len": len(clean),
            "steps": [{"usage": usage, "hold": hold} for usage, hold in clean],
        }
    return True


def enter_bootloader():
    """Ask the keyboard's firmware to restart.

    On the CH555 the ISP bootloader cannot be entered from software.  Its
    ``bBOOT_LOAD`` bit is read-only and is cleared by any software reset, so
    when the application jumps to 0xF400 the bootloader sees "application
    state" and resets straight back into the app.  The only trigger that
    leaves ``bBOOT_LOAD`` set is a power-on reset, i.e. a replug.

    This call therefore restarts the keyboard (useful on its own, and it never
    leaves the board unusable) and the flash flow waits for the replug.
    """
    if not DEV.open():
        raise RuntimeError("keyboard not connected")
    try:
        DEV.request([CMD_BOOTLOADER], timeout=200)
    except Exception:                                          # noqa: BLE001
        pass                    # the device may vanish mid-reply
    DEV.close()
    DEV.synced = False
    return True


# ------------------------------------------------------------- flashing --
ISP_ID = ("4348", "55e0")           # WCH ISP bootloader enumerated by the ROM
WINUSB_HINT = (
    " On Windows the ROM loader also needs the WinUSB driver bound to "
    "4348:55e0 (install it with Zadig, see docs/WINDOWS.md)."
    if os.name == "nt" else "")

FLASH_LOCK = threading.Lock()
FLASH = {
    "state": "idle",                # idle | waiting | writing | done | failed
    "message": "",
    "output": "",
    "ok": None,
    "attempts": 0,
    "waited": 0.0,
}


def isp_present():
    """True when the WCH ISP bootloader is on the bus."""
    if os.name == "nt":
        return _isp_present_windows()
    for entry in glob.glob("/sys/bus/usb/devices/*/idVendor"):
        folder = os.path.dirname(entry)
        try:
            with open(entry) as f:
                vid = f.read().strip()
            with open(os.path.join(folder, "idProduct")) as f:
                pid = f.read().strip()
        except OSError:
            continue
        if (vid, pid) == ISP_ID:
            return True
    return False


def _flash_set(**kw):
    with FLASH_LOCK:
        FLASH.update(kw)
        return dict(FLASH)


def flash_status():
    with FLASH_LOCK:
        return dict(FLASH)


_ISP_CACHE = {"at": 0.0, "value": False}


def _isp_present_windows():
    """Windows has no /sys, so ask the flasher that will do the work.

    `wchisp probe` scans the USB bus and reports how many WCH ISP devices it
    found.  The answer is cached briefly: the flash job polls every 200 ms and
    each probe is a process launch.
    """
    now = time.time()
    if now - _ISP_CACHE["at"] < 0.6:
        return _ISP_CACHE["value"]
    tool = _wchisp_path()
    found = False
    if tool:
        try:
            proc = subprocess.run([tool, "probe"], capture_output=True, text=True,
                                  timeout=15, creationflags=NO_WINDOW)
            text = ((proc.stdout or "") + (proc.stderr or "")).lower()
            found = "found 0 usb devices" not in text
        except (OSError, subprocess.SubprocessError):
            found = False
    _ISP_CACHE.update(at=now, value=found)
    return found


def _wchisp_path():
    exe = "wchisp.exe" if os.name == "nt" else "wchisp"
    root = os.path.dirname(ROOT)
    candidates = [
        # Inside a frozen build this is the copy the bundle carries.
        os.path.join(BUNDLE, exe) if BUNDLE else None,
        shutil.which("wchisp"),
        os.path.join(os.path.expanduser("~"), ".cargo", "bin", exe),
        os.path.join(os.path.dirname(os.path.abspath(sys.executable)), exe),
        os.path.join(root, "tools", "wchisp", "target", "release", exe),
    ]
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    return None


def _flash_paths():
    root = os.path.dirname(ROOT)
    # Prefer a wchisp the user installed (cargo install wchisp, or a distro
    # package); fall back to the in-tree build the developer machine used.
    tool = _wchisp_path()
    hexfile = os.path.join(root, "build", "keyboard.hex")
    if not tool:
        raise RuntimeError(
            "wchisp is not installed. Install it with 'cargo install wchisp' "
            "or clone https://github.com/ch32-rs/wchisp into tools/wchisp and "
            "build it with cargo (Windows: see docs/WINDOWS.md).")
    if not os.path.exists(hexfile):
        raise RuntimeError("build/keyboard.hex is missing - run `make keyboard`")
    return tool, hexfile


def _flash_job(tool, hexfile, wait_s=150.0):
    """Wait for the ROM bootloader, then write the image with wchisp."""
    start = time.time()
    if not isp_present():
        _flash_set(state="waiting", ok=None, output="", attempts=0, waited=0.0,
                   message="Fit the boot jumper, then power-cycle the keyboard: "
                           "unplug it and plug it back in. A plain replug without "
                           "the jumper leaves the ROM bootloader disabled.")
        time.sleep(0.4)             # let the UI paint the instruction
        while time.time() - start < wait_s:
            if isp_present():
                break
            _flash_set(waited=round(time.time() - start, 1))
            time.sleep(0.2)
        else:
            _flash_set(state="failed", ok=False,
                       message="The bootloader never appeared. Fit the boot jumper, "
                               "then power-cycle the keyboard and press Flash again."
                               + WINUSB_HINT)
            return

    _flash_set(state="writing", waited=round(time.time() - start, 1),
               message="Bootloader found - writing build/keyboard.hex.")
    last = None
    for attempt in range(3):
        if attempt:
            # A failed write usually means the loader timed out and the board
            # booted the application again, so waiting for the same device is
            # pointless: ask for another power cycle instead.
            _flash_set(state="waiting", attempts=attempt,
                       message="The write did not finish. Fit the boot jumper and "
                               "power-cycle the keyboard again - attempt %d of 3."
                               % (attempt + 1))
            deadline = time.time() + 60.0
            while time.time() < deadline and not isp_present():
                time.sleep(0.2)
            if not isp_present():
                last = ("the bootloader did not come back for another attempt."
                        + WINUSB_HINT)
                break
            _flash_set(state="writing", attempts=attempt,
                       message="Bootloader found - writing build/keyboard.hex.")

        try:
            proc = subprocess.run([tool, "flash", hexfile],
                                  capture_output=True, text=True, timeout=90,
                                  creationflags=NO_WINDOW)
        except subprocess.TimeoutExpired:
            last = "wchisp timed out"
            continue
        output = ((proc.stdout or "") + (proc.stderr or "")).strip()
        if proc.returncode == 0:
            _flash_set(state="done", ok=True, attempts=attempt + 1, output=output,
                       message="Firmware written. The keyboard is restarting as Velvet Mech.")
            return
        last = output or f"wchisp exited {proc.returncode}"
        time.sleep(0.5)
    _flash_set(state="failed", ok=False, output=last or "",
               message=last or "flashing failed")


def start_flash():
    """Start a background flash job; the UI polls GET /api/flash."""
    with FLASH_LOCK:
        if FLASH["state"] in ("waiting", "writing"):
            return dict(FLASH)
    tool, hexfile = _flash_paths()
    _flash_set(state="waiting", message="Preparing the flasher...", output="",
               ok=None, attempts=0, waited=0.0)
    threading.Thread(target=_flash_job, args=(tool, hexfile), daemon=True).start()
    return flash_status()


def save_all():
    if not DEV.open():
        raise RuntimeError("keyboard not connected")
    # CMD_SAVE_ALL is the only command that writes four data-flash sections
    # (settings, remap, picture, macros) before it answers, and one section is
    # already ~160 ms of erase and program.  With the 400 ms default the reply
    # arrived after the server had given up: it reopened the handle, retried,
    # wrote the same bytes three times and then reported "the keyboard rejected
    # the save packet" even though the flash was correct.
    r = DEV.request([CMD_SAVE_ALL], timeout=5000)
    if not r or len(r) < 2 or r[1] != 50:
        raise RuntimeError("the keyboard rejected the save packet")
    return True


# ------------------------------------------------------------------- sound --
# The board has no microphone: this is a host-side renderer that streams one
# colour per LED over the Direct path.  The page says so, and the firmware is
# never asked to do anything it cannot do.
AUDIO_PARAMS = {"mode": "spectrum", "palette": "prism", "gain": 50, "ceiling": 200}
AUDIO_ENGINE = None
AUDIO_SNAPSHOT = None


def _audio_rear():
    with STATE_LOCK:
        return {
            "on": bool(STATE["backOn"]),
            "level": int(STATE["backLevel"]),
            "rgb": [int(c) for c in STATE["backRgb"]],
            "ceiling": int(AUDIO_PARAMS["ceiling"]),
        }


def _audio_snapshot():
    """The lighting to put back when sound mode stops.

    Direct is a picture, not an effect, so it is restored as a frame rather
    than flattened to whichever effect happened to run before it.
    """
    with STATE_LOCK:
        if STATE["direct"]:
            return {
                "mode": DIRECT_MODE,
                "hue": STATE["hue"],
                "sat": STATE["sat"],
                "val": STATE["val"],
                "speed": STATE["speed"],
                "colours": [list(c) for c in STATE["colours"]],
            }
        effect = STATE["effect"]
        if not (0 <= effect < EFFECT_COUNT):
            effect = 2
        return {
            "effect": effect,
            "hue": STATE["hue"],
            "sat": STATE["sat"],
            "val": STATE["val"],
            "speed": STATE["speed"],
        }


def _audio_reap():
    """Restore the saved look once if the capture thread died on its own."""
    global AUDIO_ENGINE, AUDIO_SNAPSHOT
    engine = AUDIO_ENGINE
    if not engine or engine.running:
        return
    error = engine.error
    snapshot = engine.snapshot
    AUDIO_ENGINE = None
    AUDIO_SNAPSHOT = None
    try:
        if snapshot:
            apply_lighting(snapshot)
    except Exception:                                           # noqa: BLE001
        pass
    if error:
        AUDIO_PARAMS["error"] = error


def audio_start():
    global AUDIO_ENGINE, AUDIO_SNAPSHOT
    if AUDIO_ENGINE and AUDIO_ENGINE.running:
        return
    # A replug or a just-finished restart can refuse the first open while the
    # node is still settling; three quick attempts cover it without hiding a
    # genuinely absent board.
    for _ in range(3):
        if DEV.open():
            break
        time.sleep(0.15)
    else:
        raise RuntimeError("keyboard not connected")
    snapshot = _audio_snapshot()
    # Direct is the only path that takes a full frame per refresh.
    if not set_mode({"mode": DIRECT_MODE, "hue": 0, "sat": 0, "val": 255}):
        raise RuntimeError("the keyboard rejected Direct mode")
    engine = audio.Engine(set_colours, snapshot, _audio_rear())
    engine.mode = AUDIO_PARAMS["mode"]
    engine.palette = AUDIO_PARAMS["palette"]
    engine.gain = float(AUDIO_PARAMS["gain"])
    if not engine.start(AUDIO_PARAMS.get("source")):
        try:
            apply_lighting(snapshot)
        except Exception:                                       # noqa: BLE001
            pass
        raise RuntimeError(engine.error or "audio capture did not start")
    AUDIO_ENGINE = engine
    AUDIO_SNAPSHOT = snapshot
    AUDIO_PARAMS["source"] = engine.source
    AUDIO_PARAMS.pop("error", None)
    AUDIO_PARAMS.pop("note", None)


def audio_stop(reason=None):
    global AUDIO_ENGINE, AUDIO_SNAPSHOT
    engine = AUDIO_ENGINE
    snapshot = AUDIO_SNAPSHOT
    AUDIO_ENGINE = None
    AUDIO_SNAPSHOT = None
    if not engine and not snapshot:
        return
    if engine:
        engine.stop_engine()
        if engine.thread:
            engine.thread.join(timeout=1.0)
        if reason:
            AUDIO_PARAMS["note"] = reason
    if snapshot:
        try:
            apply_lighting(snapshot)
        except Exception:                                       # noqa: BLE001
            pass


def audio_apply(patch):
    """Start, stop or retune the host renderer."""
    wanted_source = patch.get("source")
    if wanted_source:
        known = {name for name, _state in audio._sources()}
        if wanted_source not in known:
            raise RuntimeError("no such capture source")
        AUDIO_PARAMS["source"] = wanted_source
    if patch.get("mode") is not None:
        if patch["mode"] not in audio.MODES:
            raise RuntimeError("unknown sound mode")
        AUDIO_PARAMS["mode"] = patch["mode"]
    if patch.get("palette") is not None:
        if patch["palette"] not in audio.PALETTES:
            raise RuntimeError("unknown palette")
        AUDIO_PARAMS["palette"] = patch["palette"]
    if patch.get("gain") is not None:
        AUDIO_PARAMS["gain"] = max(0, min(100, int(patch["gain"])))
    if patch.get("ceiling") is not None:
        AUDIO_PARAMS["ceiling"] = max(20, min(255, int(patch["ceiling"])))

    engine = AUDIO_ENGINE
    restart = False
    if engine and engine.running:
        engine.mode = AUDIO_PARAMS["mode"]
        engine.palette = AUDIO_PARAMS["palette"]
        engine.gain = float(AUDIO_PARAMS["gain"])
        engine.rear = _audio_rear()
        if wanted_source and wanted_source != engine.source:
            restart = True

    if patch.get("enabled") is False:
        audio_stop()
        return
    if restart:
        audio_stop()
    if patch.get("enabled") is True or restart:
        audio_start()


def audio_state():
    _audio_reap()
    engine = AUDIO_ENGINE
    state = {
        "params": dict(AUDIO_PARAMS),
        "running": bool(engine and engine.running),
        # While stopped, report the monitor the next Start will use, not the
        # system default: picking a source used to leave the chip unselected
        # until playback had already begun, so the choice looked ignored.
        "source": engine.source if engine else (
            AUDIO_PARAMS.get("source") or audio.monitor_source()[0]
        ),
        "error": engine.error if engine else AUDIO_PARAMS.get("error"),
        "note": AUDIO_PARAMS.get("note"),
        "level": engine.level if engine else 0.0,
        "bands": engine.bands if engine else [0.0] * audio.BANDS,
        "beat": engine.beat if engine else 0.0,
        "fps": engine.fps if engine else 0.0,
        "modes": [{"id": k, "desc": v} for k, v in audio.MODES.items()],
        "palettes": [{"id": k, "desc": v} for k, v in audio.PALETTES.items()],
        "sources": [
            {"id": name, "state": st}
            for name, st in audio._sources()
            if name.endswith(".monitor") or audio.BACKEND == "soundcard"
        ],
        "backend": audio.BACKEND,
        "targetFps": audio.TARGET_FPS,
    }
    return state


# --------------------------------------------------------------- profiles --
def load_store():
    try:
        with open(PROFILES, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        data = {}
    if "profiles" not in data:
        data = {"profiles": data if isinstance(data, dict) else {}, "keysets": {}}
    data.setdefault("profiles", {})
    data.setdefault("keysets", {})
    return data


def write_store(data):
    tmp = PROFILES + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    os.replace(tmp, PROFILES)


# ------------------------------------------------------------------- http --
class Handler(BaseHTTPRequestHandler):
    server_version = "KeyForge/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if not length:
            return {}
        try:
            return json.loads(self.rfile.read(length))
        except ValueError:
            return {}

    def _json(self, payload, status=200):
        raw = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self._cors()
        self.end_headers()
        self.wfile.write(raw)

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")

    def _static(self, rel):
        path = os.path.normpath(os.path.join(DIST, rel.lstrip("/")))
        if not path.startswith(DIST) or not os.path.isfile(path):
            if not os.path.isfile(os.path.join(DIST, "index.html")):
                return self._json(
                    {"error": "UI not built", "hint": "cd app/ui && npm install && npm run build"},
                    503,
                )
            path = os.path.join(DIST, "index.html")
        ctype = mimetypes.guess_type(path)[0] or "application/octet-stream"
        with open(path, "rb") as f:
            raw = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        # Vite fingerprints everything under /assets, so it can be cached for
        # good; the shell itself stays no-cache so a rebuild is picked up.
        cache = "public, max-age=31536000, immutable" if rel.startswith("/assets/") else "no-cache"
        self.send_header("Cache-Control", cache)
        self.end_headers()
        self.wfile.write(raw)

    def _status(self):
        connected = DEV.open()
        if connected and not DEV.synced:
            try:
                sync_from_device()
            except RuntimeError:
                connected = False
        if connected:
            try:
                refresh_live_state()
            except RuntimeError:
                connected = False
        return {
            "device": device_payload(connected),
            "lighting": lighting_payload(),
            "diag": read_diag() if connected else None,
        }

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        p = self.path.split("?")[0]
        try:
            if p == "/api/status":
                return self._json(self._status())
            if p == "/api/diag":
                return self._json(read_diag(force=True))
            if p == "/api/audio":
                return self._json(audio_state())
            if p == "/api/flash":
                return self._json(flash_status())
            if p == "/api/state":
                return self._json(lighting_payload())
            if p == "/api/config":
                lit = lighting_payload()
                return self._json({
                    "backOn": lit["backOn"],
                    "backLevel": lit["backLevel"],
                    "backRgb": lit["backRgb"],
                    "cal": lit["cal"],
                    "debounce": lit["debounce"],
                    "gaming": lit["gaming"],
                    "bootMode": lit["bootMode"],
                    "bootPace": lit["bootPace"],
                    "backMode": lit["backMode"],
                })
            if p == "/api/remap":
                with STATE_LOCK:
                    return self._json({"remap": list(STATE["remap"])})
            if p == "/api/custom":
                with STATE_LOCK:
                    return self._json({"custom": [list(c) for c in STATE["custom"]]})
            if p == "/api/macros":
                with STATE_LOCK:
                    return self._json({"macros": [dict(m) for m in STATE["macros"]]})
            if p == "/api/profiles":
                store = load_store()
                rows = [
                    {"name": name, **value}
                    for name, value in store["profiles"].items()
                ]
                rows.sort(key=lambda r: r.get("saved", ""), reverse=True)
                return self._json({"profiles": rows})
            if p == "/api/keysets":
                return self._json({"keysets": load_store()["keysets"]})
        except Exception as exc:                                    # noqa: BLE001
            return self._json({"error": str(exc)}, 500)
        self._static(p)

    def do_POST(self):
        p = self.path.split("?")[0]
        data = self._body()
        try:
            if p == "/api/audio":
                audio_apply(data)
                return self._json(audio_state())

            if p == "/api/apply":
                if AUDIO_ENGINE:
                    audio_stop("stopped - lighting was applied")
                lighting = data.get("lighting") or {}
                apply_lighting(lighting, save=bool(data.get("save")))
                if data.get("custom") is not None:
                    set_custom(data["custom"])
                return self._json({"ok": True})

            if p == "/api/save":
                if AUDIO_ENGINE:
                    audio_stop("stopped - lighting was saved")
                lighting = data.get("lighting") or lighting_payload()
                apply_lighting(lighting, save=True)
                if data.get("custom") is not None:
                    set_custom(data["custom"])
                save_all()
                return self._json({"ok": True})

            if p == "/api/config":
                set_config(data)
                return self._json({"ok": True, "config": lighting_payload()})

            if p == "/api/boot-preview":
                boot_preview()
                return self._json({"ok": True})

            if p == "/api/remap":
                if data.get("remap") is not None:
                    set_remap_all(data["remap"])
                else:
                    set_remap(data.get("index"), data.get("usage", 0xFF))
                with STATE_LOCK:
                    return self._json({"ok": True, "remap": list(STATE["remap"])})

            if p == "/api/remap/reset":
                set_remap_all([0xFF] * KEY_COUNT)
                with STATE_LOCK:
                    return self._json({"ok": True, "remap": list(STATE["remap"])})

            if p == "/api/custom":
                set_custom(data.get("custom") or [])
                return self._json({"ok": True})

            if p == "/api/macros":
                slot = data.get("slot", 0)
                set_macro(slot, data.get("key", 0xFF), data.get("steps") or [])
                with STATE_LOCK:
                    return self._json({"ok": True, "macros": [dict(m) for m in STATE["macros"]]})

            if p == "/api/bootloader":
                enter_bootloader()
                return self._json({
                    "ok": True,
                    "restarted": True,
                    "isp": isp_present(),
                    "note": "A software jump cannot hold the CH555 bootloader "
                            "(bBOOT_LOAD is read-only); flashing waits for the boot "
                            "jumper and a power-on reset.",
                })

            if p == "/api/flash":
                return self._json(start_flash())

            if p == "/api/connection":
                open_it = bool(data.get("open"))
                if not open_it:
                    audio_stop("stopped - the keyboard was released")
                DEV.paused = not open_it
                if not open_it:
                    DEV.close()
                else:
                    DEV.synced = False
                    if DEV.open():
                        sync_from_device()
                return self._json({"ok": True, "connected": bool(DEV.handle)})

            if p == "/api/profiles":
                name = (data.get("name") or "").strip()
                if not name:
                    return self._json({"error": "a profile needs a name"}, 400)
                store = load_store()
                store["profiles"][name] = {
                    "saved": time.strftime("%Y-%m-%d %H:%M"),
                    "lighting": data.get("lighting") or lighting_payload(),
                    "keyset": data.get("keyset") or [],
                }
                write_store(store)
                return self._json({"ok": True})

            if p == "/api/profiles/load":
                store = load_store()
                entry = store["profiles"].get(data.get("name") or "")
                if not entry:
                    return self._json({"error": "no such profile"}, 404)
                return self._json({"lighting": entry["lighting"], "keyset": entry.get("keyset", [])})

            if p == "/api/profiles/delete":
                store = load_store()
                store["profiles"].pop(data.get("name") or "", None)
                write_store(store)
                return self._json({"ok": True})

            if p == "/api/keysets":
                name = (data.get("name") or "").strip()
                indices = data.get("indices") or []
                if not name:
                    return self._json({"error": "a keyset needs a name"}, 400)
                store = load_store()
                store["keysets"][name] = sorted({int(i) for i in indices})
                write_store(store)
                return self._json({"ok": True})

            if p == "/api/keysets/delete":
                store = load_store()
                store["keysets"].pop(data.get("name") or "", None)
                write_store(store)
                return self._json({"ok": True})
        except Exception as exc:                                    # noqa: BLE001
            return self._json({"error": str(exc)}, 409)

        self._json({"error": "not found"}, 404)


def _message(text, title="KeyForge"):
    """Show a fatal error where the windowed build can be seen."""
    print(text)
    log(text)
    if os.name != "nt":
        return
    try:
        ctypes.windll.user32.MessageBoxW(None, text, title, 0x10)
    except Exception:                                               # noqa: BLE001
        pass


def selftest():
    """Check the things the UI needs before it can work; log and print them.

    A windowed build has no console, so this also lands in keyforge.log.  It
    is what CI runs to prove that the bundle is complete.
    """
    results = []

    def check(name, ok, detail="", fatal=True):
        results.append((name, bool(ok), fatal))
        log("selftest %-20s %s  %s" % (name, "ok  " if ok else "FAIL", detail))
        print("%-20s %s  %s" % (name, "ok  " if ok else "FAIL", detail))

    check("hidapi", LIB is not None, LIB_ERROR or "loaded")
    check("ui bundle", os.path.isfile(os.path.join(DIST, "index.html")), DIST)
    mapfile = os.path.join(os.path.dirname(ROOT), "host", "map.json")
    check("key map", os.path.isfile(mapfile), mapfile)
    hexfile = os.path.join(os.path.dirname(ROOT), "build", "keyboard.hex")
    check("firmware image", os.path.isfile(hexfile), hexfile, fatal=False)
    try:
        probe = os.path.join(DATA_DIR, ".write-test")
        with open(probe, "w", encoding="utf-8") as f:
            f.write("ok")
        os.remove(probe)
        check("data dir", True, DATA_DIR)
    except OSError as exc:
        check("data dir", False, "%s (%s)" % (DATA_DIR, exc))
    tool = _wchisp_path()
    check("wchisp", tool is not None,
          tool or "not found - flashing unavailable", fatal=False)
    if LIB is not None:
        found = enumerate_raw()
        check("keyboard", found is not None,
              found[0] if found else "not connected", fatal=False)

    failed = [name for name, ok, fatal in results if fatal and not ok]
    log("selftest %s" % ("FAILED: " + ", ".join(failed) if failed else "ok"))
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description="KeyForge local service")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--dev", action="store_true", help="point the browser at the Vite dev server")
    ap.add_argument("--selftest", action="store_true",
                    help="check hidapi and the bundled files, log the result, exit")
    args = ap.parse_args()

    log("start %s  data %s" % (sys.argv[0], DATA_DIR))

    if args.selftest:
        return selftest()

    if LIB is None:
        _message(LIB_ERROR or "hidapi is missing")
        return 1

    try:
        if DEV.open():
            sync_from_device()
    except Exception:                                               # noqa: BLE001
        pass

    try:
        server = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError as exc:
        _message("KeyForge could not start on %s:%d - %s\n\n"
                 "Another instance may already be running."
                 % (args.host, args.port, exc))
        return
    url = "http://%s:%d/" % (args.host, args.port)
    print("KeyForge service  ->  %s" % url)
    print("keyboard: %s" % (DEV.path or "not connected"))
    log("listening on %s, keyboard %s" % (url, DEV.path or "not connected"))
    if args.dev:
        print("UI:       http://127.0.0.1:5173/ (vite)")
    if not args.no_browser:
        threading.Timer(
            0.6, lambda: webbrowser.open("http://127.0.0.1:5173/" if args.dev else url)
        ).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        log("stopping")
        audio_stop()


if __name__ == "__main__":
    sys.exit(main())
