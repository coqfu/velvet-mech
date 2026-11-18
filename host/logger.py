#!/usr/bin/env python3
"""Host-side logger for the CH555 matrix detector.

The detector firmware enumerates as 1209:c555 with two HID interfaces:
  - interface 0: boot keyboard (unused in detect mode)
  - interface 1: 8-byte vendor report

Every time a key is pressed it emits:  A5 <drive-pin> <read-pin> 00 00 00 00 00

Pin numbering: 0..7 = P0.0..P0.7, 8..15 = P1.0..P1.7, 16..23 = P2.x,
24..31 = P3.x, 32..39 = P4.x.
"""
import argparse
import glob
import json
import os
import select
import sys
import time

VID, PID = 0x1209, 0xC555

# Row-major order of the 63-key 64% layout.
LAYOUT = [
    "ESC", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "MINUS", "EQUAL", "BSPC",
    "TAB", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "LBRC", "RBRC", "BSLS",
    "CAPS", "A", "S", "D", "F", "G", "H", "J", "K", "L", "SCLN", "QUOT", "ENT",
    "LSFT", "Z", "X", "C", "V", "B", "N", "M", "COMM", "DOT", "SLSH", "UP", "RSFT",
    "LCTL", "LGUI", "LALT", "SPC", "RALT", "LEFT", "DOWN", "RGHT", "FN",
]


def pin_name(p):
    return "P%d.%d" % (p // 8, p % 8)


def find_hidraw():
    candidates = []
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            uevent = open(os.path.join(path, "device", "uevent")).read()
        except OSError:
            continue
        fields = dict(
            line.split("=", 1) for line in uevent.splitlines() if "=" in line
        )
        hid_id = fields.get("HID_ID", "")
        if not hid_id:
            continue
        parts = hid_id.split(":")
        if len(parts) != 3:
            continue
        if int(parts[1], 16) != VID or int(parts[2], 16) != PID:
            continue
        dev = "/dev/" + os.path.basename(path)
        # interface 1 is the vendor report interface; the sysfs path contains
        # the USB interface, e.g. .../1-6:1.1/0003:1209:C555.000A
        real = os.path.realpath(os.path.join(path, "device"))
        candidates.append((dev, fields.get("HID_NAME", ""), ":1.1" in real))
    # vendor interface first
    candidates.sort(key=lambda c: not c[2])
    return candidates


def read_reports(dev, fd, duration, sink, quiet=None):
    """Read reports for `duration` seconds, calling sink(drive, read) per event."""
    end = time.time() + duration
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue
        try:
            data = os.read(fd, 64)
        except OSError:
            continue
        if len(data) >= 4 and data[0] == 0x00 and data[1] == 0xA5:
            data = data[1:]
        if len(data) >= 3 and data[0] == 0xA5:
            drive, read = data[1], data[2]
            sink(drive, read)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--guided", metavar="OUT.json",
                    help="walk through the layout and save the pin map")
    ap.add_argument("--scan", action="store_true",
                    help="just print every detected pair")
    args = ap.parse_args()

    cands = find_hidraw()
    if not cands:
        sys.exit("detector not found (VID 1209 PID c555). Is the keyboard "
                 "flashed with build/detect.hex and plugged in?")
    print("HID interfaces:")
    for dev, name, vendor_if in cands:
        print("  %s  %s%s" % (dev, name, "  [vendor]" if vendor_if else ""))

    # Prefer the interface whose USB interface number is 1 (the vendor one).
    fd = None
    for dev, _, vendor_if in cands:
        if vendor_if:
            fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
            print("using", dev)
            break
    if fd is None:
        for dev, _, _ in cands:
            f = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
            r, _, _ = select.select([f], [], [], 0.5)
            if r:
                fd = f
                print("using", dev)
                break
            os.close(f)
    if fd is None:
        sys.exit("no reports yet - press a key, then rerun")

    events = []

    def sink(d, r):
        events.append((d, r))
        print("  press: drive %-5s read %-5s" % (pin_name(d), pin_name(r)))

    if args.scan:
        print("press keys, Ctrl-C to stop")
        try:
            while True:
                read_reports(None, fd, 0.5, sink)
        except KeyboardInterrupt:
            pass
        return

    if not args.guided:
        sys.exit("use --scan or --guided out.json")

    mapping = []
    print("\nGuided mapping: press each key once, in the order shown.")
    print("Press ENTER when ready.")
    input()

    for name in LAYOUT:
        print("\n>> press: %s" % name)
        got = None
        last = 0.0
        while got is None:
            start = len(events)
            read_reports(None, fd, 0.05, sink)
            if len(events) > start:
                got = events[-1]
                last = time.time()
        print("   %s = drive %s / read %s" % (name, pin_name(got[0]), pin_name(got[1])))
        mapping.append({"key": name, "drive": got[0], "read": got[1]})
        # wait for release / settle, ignore stray events
        time.sleep(0.35)
        events.clear()

    with open(args.guided, "w") as f:
        json.dump(mapping, f, indent=1)
    print("\nwrote", args.guided, "(%d keys)" % len(mapping))


if __name__ == "__main__":
    main()
