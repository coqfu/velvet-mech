#!/usr/bin/env python3
"""Convert WCH's Keil C51 CH555.H into an SDCC-compatible header.

Keil:   sfr  PSW = 0xD0;          SDCC: __sfr  __at (0xD0) PSW;
Keil:   sbit EA  = IE^7;          SDCC: __sbit __at (0xAF) EA;
Keil:   sfr16 T2CON ...           dropped (paired 8-bit SFRs are kept)
"""
import re
import sys
from pathlib import Path

SRC = Path(sys.argv[1] if len(sys.argv) > 1 else
           "tools/WCH_CH555/Code/EVT_extracted/EVT/EXAM/Public/CH555.H")
DST = Path(sys.argv[2] if len(sys.argv) > 2 else "include/ch555.h")

text = SRC.read_bytes().decode("gb18030", errors="replace")
lines = text.splitlines()

sfr_addr = {}
out = []
for line in lines:
    m = re.match(r"^\s*sfr\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;", line)
    if m:
        name, addr = m.group(1), int(m.group(2), 16)
        sfr_addr[name] = addr
        out.append("__sfr __at (0x%02X) %s;" % (addr, name))
        continue

    m = re.match(r"^\s*sfr16\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;", line)
    if m:
        out.append("/* sfr16 %s dropped: use the byte registers */" % m.group(1))
        continue

    m = re.match(r"^\s*sbit\s+(\w+)\s*=\s*(\w+)\s*\^\s*(\d+)\s*;", line)
    if m:
        name, base, bit = m.group(1), m.group(2), int(m.group(3))
        if base not in sfr_addr:
            sys.exit("unknown sbit base %s for %s" % (base, name))
        addr = sfr_addr[base] + bit
        if not (0x80 <= addr <= 0xFF):
            sys.exit("bad sbit address for %s: 0x%02X" % (name, addr))
        out.append("__sbit __at (0x%02X) %s;" % (addr, name))
        continue

    out.append(line)

text = "\n".join(out)

# Storage-class keywords.
for keil, sdcc in (("bit", "__bit"), ("xdata", "__xdata"), ("code", "__code"),
                   ("data", "__data"), ("idata", "__idata"), ("pdata", "__pdata")):
    text = re.sub(r"(?<![\w$])%s(?![\w$])" % keil, sdcc, text)

# SDCC has no absacc.h on every install; define the accessors ourselves.
text = text.replace('#ifndef __BASE_TYPE__',
                    '#ifndef XBYTE\n'
                    '#define XBYTE ((unsigned char volatile __xdata *)0)\n'
                    '#endif\n'
                    '#ifndef PBYTE\n'
                    '#define PBYTE ((unsigned char volatile __pdata *)0)\n'
                    '#endif\n'
                    '#define _nop_() __asm nop __endasm\n'
                    '\n'
                    '#ifndef __BASE_TYPE__', 1)

DST.parent.mkdir(parents=True, exist_ok=True)
DST.write_text("/* AUTO-GENERATED from %s -- do not edit */\n" % SRC.name + text + "\n")
print("wrote", DST, "(%d sfr, %d lines)" % (len(sfr_addr), len(out)))
