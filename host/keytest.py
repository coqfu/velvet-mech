#!/usr/bin/env python3
"""Read key events from the CH555 keyboard's evdev node.

Prints one line per key transition so the firmware can be verified without
looking at the screen.  Structure of struct input_event on 64-bit Linux:
    timeval { long sec, long usec }  (16 bytes)
    __u16 type, __u16 code, __s32 value
"""
import glob
import os
import select
import struct
import sys
import time

EV_SYN, EV_KEY, EV_MSC = 0, 1, 4

NAMES = {
    1: "ESC", 2: "1", 3: "2", 4: "3", 5: "4", 6: "5", 7: "6", 8: "7",
    9: "8", 10: "9", 11: "0", 12: "MINUS", 13: "EQUAL", 14: "BSPC",
    15: "TAB", 16: "Q", 17: "W", 18: "E", 19: "R", 20: "T", 21: "Y",
    22: "U", 23: "I", 24: "O", 25: "P", 26: "LBRC", 27: "RBRC",
    28: "ENT", 29: "LCTL", 30: "A", 31: "S", 32: "D", 33: "F", 34: "G",
    35: "H", 36: "J", 37: "K", 38: "L", 39: "SCLN", 40: "QUOT",
    41: "GRAVE", 42: "LSFT", 43: "BSLS", 44: "Z", 45: "X", 46: "C",
    47: "V", 48: "B", 49: "N", 50: "M", 51: "COMM", 52: "DOT",
    53: "SLSH", 54: "RSFT", 55: "KPASTERISK", 56: "LALT", 57: "SPC",
    58: "CAPS", 59: "F1", 60: "F2", 61: "F3", 62: "F4", 63: "F5",
    64: "F6", 65: "F7", 66: "F8", 67: "F9", 68: "F10", 69: "NUMLK",
    70: "SCROLL", 87: "F11", 88: "F12", 96: "KPENTER", 97: "RCTL",
    98: "KPSLASH", 99: "PRTSC", 100: "RALT", 102: "HOME", 103: "UP",
    104: "PGUP", 105: "LEFT", 106: "RGHT", 107: "END", 108: "DOWN",
    109: "PGDN", 110: "INS", 111: "DEL", 113: "MUTE", 114: "VOL-",
    115: "VOL+", 116: "POWER", 119: "PAUSE", 125: "LWIN", 126: "RWIN",
    127: "MENU", 163: "NEXTSONG", 164: "PLAYPAUSE", 165: "PREVSONG",
    166: "STOPCD", 172: "HOMEPAGE", 113: "MUTE",
}

EVENT_BYTES = struct.calcsize("llHHi")


def find_event_nodes(name_hint="Velvet"):
    nodes = []
    for path in sorted(glob.glob("/sys/class/input/event*")):
        try:
            name = open(os.path.join(path, "device", "name")).read().strip()
        except OSError:
            continue
        if name_hint.lower() in name.lower():
            nodes.append(("/dev/input/" + os.path.basename(path), name))
    return nodes


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
    nodes = find_event_nodes()
    if not nodes:
        sys.exit("no CH555 input device found")
    for dev, name in nodes:
        print("event node: %s  (%s)" % (dev, name), flush=True)

    # The consumer interface also exposes a node; watch all of them.
    fds = {}
    for dev, _ in nodes:
        try:
            fds[os.open(dev, os.O_RDONLY | os.O_NONBLOCK)] = dev
        except OSError:
            pass
    if not fds:
        sys.exit("cannot open event nodes (permissions?)")

    end = time.time() + duration
    while time.time() < end:
        r, _, _ = select.select(list(fds), [], [], 0.1)
        for fd in r:
            data = os.read(fd, EVENT_BYTES * 32)
            for off in range(0, len(data) - EVENT_BYTES + 1, EVENT_BYTES):
                sec, usec, typ, code, val = struct.unpack(
                    "llHHi", data[off:off + EVENT_BYTES])
                if typ != EV_KEY:
                    continue
                label = NAMES.get(code, "code%d" % code)
                print("%8.3f  %-11s %-10s %s" % (
                    sec + usec / 1e6, fds[fd].split("/")[-1], label,
                    "DOWN" if val else "up"), flush=True)


if __name__ == "__main__":
    main()
