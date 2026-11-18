#!/usr/bin/env bash
# Wait for the WCH ISP bootloader (which only appears on a power-on reset)
# and flash the given hex as soon as it answers.
set -u

hex=${1:?usage: flash-on-replug.sh FILE.hex}

here=$(cd "$(dirname "$0")/.." && pwd)
wchisp="$here/tools/wchisp/target/release/wchisp"

# Resolve the hex before touching the hardware.  This script finds the flasher
# relative to the repository, so the firmware has to be resolved the same way:
# the caller may be in their home directory, where "build/keyboard.hex" is a
# different, non-existent file.  wchisp reports that as a bare
# "No such file or directory (os error 2)" *after* printing the chip info,
# which reads like a USB failure.  Catch it here instead.
if [ -f "$hex" ]; then
    hex="$(cd "$(dirname "$hex")" && pwd)/$(basename "$hex")"
elif [ -f "$here/$hex" ]; then
    hex="$here/$hex"
else
    echo "no such file: $hex" >&2
    echo "  also tried: $here/$hex" >&2
    echo "  build it first: make keyboard" >&2
    exit 2
fi

if [ ! -x "$wchisp" ]; then
    echo "missing flasher: $wchisp" >&2
    echo "  build it: cd $here/tools/wchisp && cargo build --release" >&2
    exit 2
fi

echo "firmware: $hex"
echo "waiting for 4348:55e0 ..."
for _ in $(seq 1 2400); do
    if lsusb -d 4348:55e0 >/dev/null 2>&1; then
        sleep 0.4
        echo "bootloader found, flashing $hex"
        # The bootloader can enumerate a moment before its endpoints answer,
        # and a wedged one needs the user to power-cycle again, so retry.
        # Only genuine write failures get here: the file was checked above.
        for attempt in $(seq 1 15); do
            if "$wchisp" flash "$hex"; then
                echo "flash ok"
                exit 0
            fi
            echo "attempt $attempt failed, retrying in 0.5s"
            sleep 0.5
        done
        echo "bootloader did not answer; unplug and replug the keyboard" >&2
        exit 1
    fi
    sleep 0.05
done
echo "timed out waiting for the bootloader" >&2
exit 1
