#!/usr/bin/env python3
"""Talk to the keyboard's OpenRGB raw-HID interface the same way OpenRGB does.

Uses libhidapi-hidraw so the byte framing matches OpenRGB exactly (65-byte
writes/reads: report id 0 followed by the 64-byte report).

    tools/openrgb-test.py [--path /dev/hidrawN] [--info | --static | --modes | --underglow]

The default run sends a moving rainbow over the whole board for six seconds.
"""

import ctypes
import sys
import time

PACKET = 65
CMD_PROTOCOL = 1
CMD_QMK_VERSION = 2
CMD_DEVICE_INFO = 3
CMD_MODE_INFO = 4
CMD_LED_INFO = 5
CMD_ENABLED_MODES = 6
CMD_SET_MODE = 7
DIRECT = 6          # OpenRGB position of Direct in the advertised list
CMD_SET_SINGLE_LED = 8
CMD_SET_LEDS = 9

SUCCESS = 50
FAILURE = 25
END = 100

KEY_COUNT = 63
UNDERGLOW_COUNT = 6
LED_COUNT = KEY_COUNT + UNDERGLOW_COUNT

FLAG_UNDERGLOW = 0x02
FLAG_KEYLIGHT = 0x04


def load():
    for name in ("libhidapi-hidraw.so", "libhidapi-hidraw.so.0"):
        try:
            return ctypes.CDLL(name)
        except OSError:
            pass
    for name in ("libhidapi-libusb.so", "libhidapi-libusb.so.0"):
        try:
            return ctypes.CDLL(name)
        except OSError:
            pass
    raise SystemExit("hidapi not found")


def find_velvet(lib):
    """Locate the raw HID collection by usage page 0xFF60 / usage 0x61."""
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
    found = []
    while node:
        i = node.contents
        if i.usage_page == 0xFF60 and i.usage == 0x61:
            found.append(i.path.decode())
        node = i.next
    lib.hid_free_enumeration(head)
    return found


class Dev:
    def __init__(self, lib, path):
        lib.hid_open_path.restype = ctypes.c_void_p
        lib.hid_open_path.argtypes = [ctypes.c_char_p]
        self.lib = lib
        self.handle = lib.hid_open_path(path.encode())
        if not self.handle:
            raise SystemExit("cannot open " + path)
        lib.hid_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        lib.hid_read_timeout.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int
        ]
        lib.hid_read_timeout.restype = ctypes.c_int

    def request(self, data, timeout=300):
        buf = bytearray(PACKET)
        buf[0] = 0
        buf[1:1 + len(data)] = data
        self.lib.hid_write(self.handle, bytes(buf), PACKET)
        out = ctypes.create_string_buffer(PACKET)
        n = self.lib.hid_read_timeout(self.handle, out, PACKET, timeout)
        if n <= 0:
            return None
        return list(out.raw[:n])

    def send(self, data):
        buf = bytearray(PACKET)
        buf[0] = 0
        buf[1:1 + len(data)] = data
        self.lib.hid_write(self.handle, bytes(buf), PACKET)


def read_leds(dev, first, count):
    """GET_LED_INFO returns 8 seven-byte records per packet, starting at byte 1."""
    out = []
    sent = 0
    while sent < count:
        n = min(8, count - sent)
        r = dev.request([CMD_LED_INFO, first + sent, n])
        if not r:
            raise SystemExit("no reply to GET_LED_INFO")
        if r[0] != CMD_LED_INFO:
            raise SystemExit("GET_LED_INFO reply echoes %d" % r[0])
        for k in range(n):
            o = 1 + k * 7
            out.append({
                "x": r[o], "y": r[o + 1], "flags": r[o + 2],
                "rgb": (r[o + 3], r[o + 4], r[o + 5]), "key": r[o + 6],
            })
        sent += n
    return out


def set_leds(dev, colours):
    """SET_LEDS carries at most 15 LEDs per packet and gets no reply."""
    sent = 0
    while sent < len(colours):
        n = min(15, len(colours) - sent)
        payload = [CMD_SET_LEDS, n]
        for k in range(n):
            led = sent + k
            r, g, b = colours[led]
            payload += [led, r, g, b]
        dev.send(payload)
        sent += n


def hsv(hue, sat=255, val=255):
    region = hue // 43
    rem = (hue - region * 43) * 6
    p, q, t = 0, 255 - rem, rem
    table = [(255, t, 0), (q, 255, 0), (0, 255, t),
             (0, q, 255), (t, 0, 255), (255, 0, q)]
    r, g, b = table[region % 6]
    return (r * val // 255, g * val // 255, b * val // 255)


def info(dev):
    r = dev.request([CMD_PROTOCOL])
    if not r:
        raise SystemExit("no reply to GET_PROTOCOL_VERSION")
    print("protocol version : 0x%02X  (64-byte reply: %s)" % (r[1], len(r) == 64))
    assert r[1] == 0x0D, "expected revision D"

    r = dev.request([CMD_QMK_VERSION])
    print("qmk version      :", bytes(r[1:r.index(0, 1)]).decode(errors="replace"))

    r = dev.request([CMD_DEVICE_INFO])
    leds = r[1]
    i = 3
    name = bytes(r[i:r.index(0, i)]).decode(errors="replace")
    i = r.index(0, i) + 1
    vendor = bytes(r[i:r.index(0, i)]).decode(errors="replace")
    print("device           : %r by %r, %d LEDs" % (name, vendor, leds))
    assert name == "Velvet Mech", "unexpected device name"
    assert leds == LED_COUNT, "expected %d LEDs, got %d" % (LED_COUNT, leds)

    r = dev.request([CMD_MODE_INFO])
    print("mode/speed/hsv   : %d %d %d %d %d" % tuple(r[1:6]))

    r = dev.request([CMD_ENABLED_MODES])
    modes = r[1:r.index(0, 1)]
    print("enabled modes    :", modes)

    # OpenRGB numbers the modes it builds by their position, and creates Direct
    # last (its UI entry is then moved to the front).  Print the numbering it
    # will actually put on the wire so a mismatch is visible immediately.
    named = {1: "Direct", 2: "Static", 6: "Breathing", 13: "Cycle All",
             28: "Hue Wave", 32: "Solid Reactive"}
    positions = {m: n + 1 for n, m in enumerate(sorted(m for m in modes if m != 1))}
    positions[1] = len(positions) + 1
    print("mode positions   :",
          ", ".join("%s=%d" % (named.get(m, str(m)), positions[m]) for m in positions),
          "(wire ->", ", ".join("%d:%s" % (p, named.get(m, m)) for m, p in sorted(positions.items(), key=lambda kv: kv[1])), ")")

    leds = read_leds(dev, 0, LED_COUNT)
    keys = [l for l in leds if l["flags"] == FLAG_KEYLIGHT]
    glow = [l for l in leds if l["flags"] == FLAG_UNDERGLOW]
    holes = [l for l in leds if l["flags"] == FAILURE]
    print("zones            : %d keylight, %d underglow, %d holes"
          % (len(keys), len(glow), len(holes)))
    for n, l in enumerate(leds):
        if n < 4 or n >= LED_COUNT - UNDERGLOW_COUNT:
            print("  led %2d  x=%3d y=%3d flags=%02X rgb=%02X%02X%02X key=%02X"
                  % (n, l["x"], l["y"], l["flags"], *l["rgb"], l["key"]))
    return leds


def main():
    args = sys.argv[1:]
    lib = load()
    lib.hid_init()

    if "--path" in args:
        path = args[args.index("--path") + 1]
    else:
        paths = find_velvet(lib)
        if not paths:
            raise SystemExit("no raw HID collection with usage page 0xFF60 / 0x61")
        path = paths[0]

    print("using", path)
    dev = Dev(lib, path)

    if "--info" in args:
        info(dev)
        return

    # Direct mode, like OpenRGB does when you pick "Direct".  Mode numbers are
    # OpenRGB's positions in the advertised list, not QMK ids: Direct is 6.
    r = dev.request([CMD_SET_MODE, 0, 0, 0, DIRECT, 0x7F, 0])
    print("set mode direct  : reply %s" % (r[1] if r else None))
    assert r and r[1] == SUCCESS, "SET_MODE did not succeed"

    if "--modes" in args:
        for mode, label in ((1, "Static (orange)"),
                            (2, "Breathing"),
                            (3, "Cycle All"),
                            (4, "Hue Wave"),
                            (5, "Solid Reactive")):
            h, s, v = (20, 255, 200) if mode == 1 else (0, 255, 255)
            r = dev.request([CMD_SET_MODE, h, s, v, mode, 0x7F, 0])
            print("mode %2d %-18s reply %s" % (mode, label, r[1] if r else None))
            time.sleep(1.5)
        dev.request([CMD_SET_MODE, 0, 0, 0, DIRECT, 0x7F, 0])
        print("back to Direct")
        return

    if "--underglow" in args:
        # Keys dim white, one strip LED at a time in a bright colour, so the
        # mapping between zone LED number and the physical light is obvious.
        print("walking the six underglow LEDs, 1.2 s each ...")
        for n in range(UNDERGLOW_COUNT):
            colours = [(12, 12, 12)] * KEY_COUNT
            for k in range(UNDERGLOW_COUNT):
                colours.append((255, 255, 255) if k == n else (0, 0, 0))
            set_leds(dev, colours)
            time.sleep(1.2)
        colours = [(255, 0, 120)] * KEY_COUNT + [(0, 220, 255)] * UNDERGLOW_COUNT
        set_leds(dev, colours)
        print("done - keys magenta, strip cyan")
        return

    if "--static" in args:
        # Paint all LEDs one by one, then read them back.  SET_SINGLE_LED is
        # answered, exactly like OpenRGB reads the answer.
        for led in range(LED_COUNT):
            r = dev.request([CMD_SET_SINGLE_LED, led, led * 4 & 0xFF,
                             255 - (led * 4) & 0xFF, 128])
            assert r and r[1] == SUCCESS, "SET_SINGLE_LED %d failed" % led
        time.sleep(0.3)
        leds = read_leds(dev, 0, LED_COUNT)
        bad = 0
        for n, l in enumerate(leds):
            want = (n * 4 & 0xFF, 255 - (n * 4) & 0xFF, 128)
            if l["rgb"] != want:
                bad += 1
                print("  mismatch led %2d: got %02X%02X%02X want %02X%02X%02X"
                      % (n, *l["rgb"], *want))
        print("readback         : %d/%d LEDs match" % (LED_COUNT - bad, LED_COUNT))
        print("keys and strip should now be a rainbow gradient")
        return

    # A moving rainbow band over the whole board, 15 LEDs per packet.
    print("sending a colour sweep for 6 s ...")
    t0 = time.time()
    frame = 0
    while time.time() - t0 < 6:
        colours = []
        for led in range(LED_COUNT):
            colours.append(hsv((led * 4 + frame * 6) & 0xFF))
        set_leds(dev, colours)
        frame += 1
        time.sleep(0.03)
    print("done")


if __name__ == "__main__":
    main()
