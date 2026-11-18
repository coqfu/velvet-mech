#!/usr/bin/env python3
"""Capture distinct matrix pin pairs from the CH555 detector.

Reads the vendor HID interface and collapses switch bounce into one event per
physical press. Prints one line per new pair:

    <index>  <drive> -> <read>   (+t <ms since start>)

Usage:  python3 host/capture.py [seconds]
"""
import os
import select
import sys
import time

from logger import find_hidraw, pin_name

BOUNCE_MS = float(os.environ.get("BOUNCE_MS", "250"))


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0

    cands = find_hidraw()
    dev = None
    for d, _, vendor_if in cands:
        if vendor_if:
            dev = d
            break
    if dev is None and cands:
        dev = cands[0][0]
    if dev is None:
        sys.exit("detector not found (1209:c555)")

    fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
    print("reading", dev, flush=True)

    start = time.time()
    end = start + duration
    last_pair = None
    last_t = 0.0
    seen = []

    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        try:
            data = os.read(fd, 64)
        except OSError:
            continue
        if len(data) >= 4 and data[0] == 0x00 and data[1] == 0xA5:
            data = data[1:]
        if len(data) < 3 or data[0] != 0xA5:
            continue
        pair = (data[1], data[2])
        now = time.time()
        if pair == last_pair and (now - last_t) * 1000 < BOUNCE_MS:
            last_t = now
            continue
        last_pair = pair
        last_t = now
        seen.append(pair)
        print("%2d  %-6s -> %-6s   t=%5dms" % (
            len(seen), pin_name(pair[0]), pin_name(pair[1]),
            int((now - start) * 1000)), flush=True)

    print("total distinct events:", len(seen))


if __name__ == "__main__":
    main()
