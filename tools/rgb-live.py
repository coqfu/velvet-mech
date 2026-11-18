#!/usr/bin/env python3
"""Live capture of everything the keyboard reports while it is being glitched.

Two HID interfaces are opened at the same time:

  * the raw HID collection (usage page 0xFF60 / usage 0x61) is polled the way
    OpenRGB polls it -- GET_LED_INFO sweeps, GET_MODE_INFO, GET_PROTOCOL --
    with a monotonic timestamp on every request and reply;
  * the boot-keyboard collection (usage page 1 / usage 6) is read
    continuously, so key events are timestamped too.

Every reply is checked for length, command echo and end-of-message byte, and
LED records are compared against a baseline captured at start-up.  Anything
that differs, any request whose reply is late, and any key event is appended
to the log.  A status line is printed every two seconds.

    tools/rgb-live.py [--log /tmp/opencode/rgb-live.log] [--seconds N]

Run it (detached) before touching the keyboard, then reproduce the glitch.
The log is line buffered and flushed on every event.
"""

import ctypes
import os
import sys
import time

PACKET = 65
CMD_PROTOCOL = 1
CMD_MODE_INFO = 4
CMD_LED_INFO = 5
CMD_ENABLED_MODES = 6
END = 100
LED_COUNT = 69
LATE_MS = 10.0          # a reply slower than this is worth a log line
SWEEP_PAUSE = 0.0

LOG = "/tmp/opencode/rgb-live.log"
SECONDS = 0             # 0 = run until killed


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


def load():
    for name in ("libhidapi-hidraw.so", "libhidapi-hidraw.so.0",
                 "libhidapi-libusb.so", "libhidapi-libusb.so.0"):
        try:
            return ctypes.CDLL(name)
        except OSError:
            pass
    raise SystemExit("hidapi not found")


def enumerate_velvet(lib):
    """Return [(path, usage_page, usage, interface)] for our device."""
    lib.hid_enumerate.restype = ctypes.POINTER(Info)
    lib.hid_enumerate.argtypes = [ctypes.c_ushort, ctypes.c_ushort]
    head = lib.hid_enumerate(0x1209, 0xC555)
    out = []
    node = head
    while node:
        i = node.contents
        out.append((i.path.decode(), i.usage_page, i.usage,
                    i.interface_number))
        node = i.next
    lib.hid_free_enumeration(head)
    return out


class Handle:
    def __init__(self, lib, path):
        lib.hid_open_path.restype = ctypes.c_void_p
        lib.hid_open_path.argtypes = [ctypes.c_char_p]
        self.lib = lib
        self.h = lib.hid_open_path(path.encode())
        if not self.h:
            raise SystemExit("cannot open " + path)
        lib.hid_write.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                  ctypes.c_size_t]
        lib.hid_read_timeout.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                         ctypes.c_size_t, ctypes.c_int]
        lib.hid_read_timeout.restype = ctypes.c_int
        lib.hid_set_nonblocking.argtypes = [ctypes.c_void_p, ctypes.c_int]

    def write(self, data):
        buf = bytearray(PACKET)
        buf[1:1 + len(data)] = data
        return self.lib.hid_write(self.h, bytes(buf), PACKET)

    def read(self, timeout=300):
        out = ctypes.create_string_buffer(PACKET)
        n = self.lib.hid_read_timeout(self.h, out, PACKET, timeout)
        if n <= 0:
            return None
        return list(out.raw[:n])

    def nonblocking(self, on):
        self.lib.hid_set_nonblocking(self.h, 1 if on else 0)


def now():
    return time.time()


def stamp(t0):
    return time.strftime("%H:%M:%S", time.localtime()) + ".%03d" % (
        int((time.time() % 1) * 1000)) + " +%.3f" % (time.perf_counter() - t0)


def main():
    global LOG, SECONDS
    args = sys.argv[1:]
    while args:
        if args[0] == "--log":
            LOG = args[1]
            args = args[2:]
        elif args[0] == "--seconds":
            SECONDS = int(args[1])
            args = args[2:]
        else:
            raise SystemExit("unknown argument " + args[0])

    lib = load()

    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    log = open(LOG, "w", buffering=1)
    t0 = time.perf_counter()

    def emit(kind, text):
        log.write("%s %-6s %s\n" % (stamp(t0), kind, text))

    def open_device():
        interfaces = enumerate_velvet(lib)
        raw = [p for p, page, usage, _ in interfaces
               if page == 0xFF60 and usage == 0x61]
        kbd = [p for p, page, usage, _ in interfaces
               if page == 1 and usage == 6]
        if not raw:
            return None, None
        dev = Handle(lib, raw[0])
        board = Handle(lib, kbd[0]) if kbd else None
        if board:
            board.nonblocking(True)
        return dev, board

    def baseline_of(dev):
        base = {}
        errors = 0
        for first in range(0, LED_COUNT, 8):
            count = min(8, LED_COUNT - first)
            dev.write([CMD_LED_INFO, first, count])
            r = dev.read()
            if not r:
                errors += 1
                continue
            base[first] = r
        return base, errors

    dev, board = None, None
    for _ in range(50):
        dev, board = open_device()
        if dev:
            break
        time.sleep(0.2)
    if not dev:
        raise SystemExit("raw HID collection not found")
    emit("start", "capture running")

    baseline, sweep_errors = baseline_of(dev)
    emit("base", "led pages=%d sweep_errors=%d"
         % (len(baseline), sweep_errors))

    last_keys = None

    requests = 0
    anomalies = 0
    late = 0
    slowest = 0.0
    status_at = time.time()
    started = time.time()
    pages = sorted(baseline)

    while True:
        if SECONDS and time.time() - started > SECONDS:
            break

        # Keyboard report, if anything arrived since the last loop.
        if board:
            while True:
                rep = board.read(timeout=0)
                if rep is None:
                    break
                if rep != last_keys:
                    last_keys = rep
                    emit("keys", " ".join("%02x" % b for b in rep))

        first = pages[requests % len(pages)]
        count = min(8, LED_COUNT - first)
        want = [CMD_LED_INFO, first, count]

        t = time.perf_counter()
        wrote = dev.write(want)
        r = dev.read(timeout=300)
        dt = (time.perf_counter() - t) * 1000.0
        requests += 1
        slowest = max(slowest, dt)
        if wrote < 0:
            # The device went away (unplug, reset, or the host stopped the
            # pipe).  Log it, then wait for it to come back.
            anomalies += 1
            emit("LOST", "raw write failed, waiting for the device")
            while True:
                time.sleep(0.2)
                dev, board = open_device()
                if dev:
                    baseline, sweep_errors = baseline_of(dev)
                    pages = sorted(baseline)
                    emit("found", "pages=%d sweep_errors=%d"
                         % (len(baseline), sweep_errors))
                    break
            continue

        if r is None:
            anomalies += 1
            emit("TIMEOUT", "page=%d latency=%.1fms" % (first, dt))
        else:
            problem = None
            if len(r) != 64:
                problem = "length %d" % len(r)
            elif r[0] != CMD_LED_INFO:
                problem = "echo %02x" % r[0]
            elif r[63] != END:
                problem = "end byte %02x" % r[63]
            else:
                base = baseline[first]
                if r[:57] != base[:57]:
                    bad = [i for i in range(57) if r[i] != base[i]]
                    problem = "payload differs at %d (%02x vs %02x)" % (
                        bad[0], r[bad[0]], base[bad[0]])
                    if bad[0] % 7 == 0:
                        problem += " -> first byte of a record"
            if problem:
                anomalies += 1
                emit("BAD", "page=%d latency=%.1fms %s"
                     % (first, dt, problem))
                emit("BAD", "  got " + " ".join("%02x" % b for b in r))
                emit("BAD", "  exp " + " ".join("%02x" % b
                                                for b in baseline[first]))
            if dt >= LATE_MS:
                late += 1
                emit("LATE", "page=%d latency=%.1fms" % (first, dt))

        # Small state query so mode/brightness/speed changes are timestamped.
        if requests % 64 == 0:
            t = time.perf_counter()
            dev.write([CMD_MODE_INFO])
            r = dev.read(timeout=300)
            dt = (time.perf_counter() - t) * 1000.0
            if r is None:
                anomalies += 1
                emit("TIMEOUT", "mode latency=%.1fms" % dt)
            elif len(r) != 64 or r[63] != END or r[0] != CMD_MODE_INFO:
                anomalies += 1
                emit("BAD", "mode " + " ".join("%02x" % b for b in r))
            if r and len(r) > 6:
                emit("mode", "mode=%d speed=%d hue=%d sat=%d val=%d"
                     % (r[1], r[2], r[3], r[4], r[5]))

        if requests % 128 == 0:
            t = time.perf_counter()
            dev.write([CMD_PROTOCOL])
            r = dev.read(timeout=300)
            dt = (time.perf_counter() - t) * 1000.0
            if r is None or len(r) != 64 or r[0] != CMD_PROTOCOL or r[1] != 0x0D:
                anomalies += 1
                emit("RESET?", "protocol reply %s"
                     % ("none" if r is None else
                        " ".join("%02x" % b for b in r[:8])))

        if SWEEP_PAUSE:
            time.sleep(SWEEP_PAUSE)

        if time.time() - status_at >= 2.0:
            status_at = time.time()
            print("%s requests=%d anomalies=%d late=%d slowest=%.1fms"
                  % (stamp(t0), requests, anomalies, late, slowest),
                  flush=True)

    emit("stop", "requests=%d anomalies=%d late=%d slowest=%.1fms"
         % (requests, anomalies, late, slowest))
    print("capture written to " + LOG, flush=True)


if __name__ == "__main__":
    main()
