#!/usr/bin/env python3
"""Low-level CH555 ISP probe: figure out why the WCH bootloader does not answer."""
import sys
import time

import usb.core
import usb.util

VID, PID = 0x4348, 0x55E0


def find():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("no 4348:55e0 device found")
    return dev


def show(dev):
    print("device:", dev)
    print("bus %d addr %d speed %s" % (dev.bus, dev.address, dev.speed))
    try:
        print("active config:", dev.get_active_configuration().bConfigurationValue)
    except usb.core.USBError as e:
        print("active config error:", e)
    for cfg in dev:
        print("config", cfg.bConfigurationValue, "interfaces", cfg.bNumInterfaces)
        for intf in cfg:
            print("  intf", intf.bInterfaceNumber, "class",
                  hex(intf.bInterfaceClass), "eps", intf.bNumEndpoints)
            for ep in intf:
                print("    ep", hex(ep.bEndpointAddress), "attr", ep.bmAttributes,
                      "maxpkt", ep.wMaxPacketSize)


def try_ep(dev, out_ep, in_ep):
    cmd = bytes([0xA1, 0x12, 0x00, 0x00, 0x11]) + b"MCU ISP & WCH.CN"
    print("write %s -> %s" % (cmd.hex(), hex(out_ep)))
    try:
        n = dev.write(out_ep, cmd, timeout=2000)
        print("  wrote", n)
    except usb.core.USBError as e:
        print("  write error:", e)
        return
    try:
        rx = dev.read(in_ep, 64, timeout=2000)
        print("  read", bytes(rx).hex())
    except usb.core.USBError as e:
        print("  read error:", e)


def main():
    dev = find()
    show(dev)
    usb.util.claim_interface(dev, 0)
    for ep in (0x83, 0x82):
        try:
            usb.util.clear_halt(dev, ep)
        except Exception:
            pass
    try_ep(dev, 0x02, 0x83)
    try_ep(dev, 0x02, 0x82)


if __name__ == "__main__":
    main()
