#!/usr/bin/env python3
"""Fail the build if an interrupt path touches SDCC's shared overlay block.

SDCC keeps the parameters and locals of every non-reentrant function in one
9-byte overlay area (0x73..0x7B on this build).  The main loop and the
interrupts share that area: when an ISR calls a function that owns an overlay
symbol, it silently overwrites whatever the interrupted main-loop function
kept there.

That is not theoretical.  led_irq() used to compute

    led_dma = (UINT16)&led_buf[led_show][0];

which compiles to a call to __mulint whose parameter block sits at 0x73 --
exactly where raw_hid_send() keeps the source pointer for the OpenRGB reply it
is copying.  Roughly once per LED frame the pointer was replaced by 0x0000, so
the reply was truncated and filled with bytes from the raw request buffer.

The check compiles each module to assembly, builds the call graph from the
three interrupt handlers, and rejects any reachable function that mentions an
overlay symbol (``_*_PARM_*``), an overlay address (0x73..0x7B), or a call to
one of SDCC's pointer/maths helpers that use those addresses.

Run from the project root; exits 0 when clean.
"""

import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM_DIR = os.path.join(ROOT, "build", "isrcheck")
MODULES = ["main", "usb", "board", "keyboard", "keymap", "led", "settings",
           "openrgb"]
ROOTS = ["USB_DeviceInterrupt", "timer0_isr", "led_isr"]
CFLAGS = ["-mmcs51", "--model-small", "-Iinclude", "-DOPENRGB"]

# SDCC helpers whose parameters are placed in the overlay area.
OVERLAY_HELPERS = re.compile(
    r"\b_(?:mulint|divuint|divsint|moduint|modsint|gptrget|gptrput|"
    r"memcpy|memset)\b")
OVERLAY_PARAM = re.compile(r"\b_[A-Za-z0-9_]+_PARM[A-Za-z0-9_]*\b")
OVERLAY_ADDR = re.compile(r"\b0x7[3-9a-b]\b|\(\s*0x7[3-9a-b]\s*\+")


def compile_modules():
    os.makedirs(ASM_DIR, exist_ok=True)
    paths = []
    for mod in MODULES:
        src = os.path.join(ROOT, "src", mod + ".c")
        out = os.path.join(ASM_DIR, mod + ".asm")
        subprocess.run(["sdcc", *CFLAGS, "-S", src, "-o", out],
                       check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
        paths.append(out)
    return paths


def parse(paths):
    """Return {function: [instruction lines]} for every module."""
    funcs = {}
    for path in paths:
        current = None
        for line in open(path, errors="replace"):
            match = re.match(r"^_([A-Za-z0-9_]+):", line)
            if match:
                current = match.group(1)
                funcs[current] = []
            elif current is not None:
                funcs[current].append(line)
    return funcs


def call_graph(funcs):
    graph = {}
    for name, body in funcs.items():
        targets = set()
        for line in body:
            for match in re.finditer(r"(?:lcall|ljmp)\s+(_[A-Za-z0-9_]+)",
                                     line):
                targets.add(match.group(1)[1:])
        graph[name] = targets
    return graph


def reachable(graph, funcs, roots):
    seen = set()
    stack = list(roots)
    while stack:
        name = stack.pop()
        if name in seen or name not in funcs:
            continue
        seen.add(name)
        stack.extend(graph.get(name, ()))
    return seen


def main():
    funcs = parse(compile_modules())
    graph = call_graph(funcs)

    problems = []
    for root in ROOTS:
        if root not in funcs:
            problems.append("%s: interrupt handler not found" % root)
            continue
        for name in sorted(reachable(graph, funcs, [root])):
            for line in funcs[name]:
                text = line.split(";", 1)[0]
                if not text.strip():
                    continue
                if OVERLAY_PARAM.search(text) or OVERLAY_ADDR.search(text):
                    problems.append("%s -> %s: %s"
                                    % (root, name, line.strip()))
                elif OVERLAY_HELPERS.search(text):
                    problems.append("%s -> %s: %s"
                                    % (root, name, line.strip()))

    if problems:
        print("ISR overlay check FAILED:", file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        print("\nMove the offending code out of the interrupt path, use "
              "__reentrant,\nor replace the helper call with register-only "
              "arithmetic.", file=sys.stderr)
        return 1

    print("ISR overlay check: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
