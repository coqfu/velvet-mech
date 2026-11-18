#!/usr/bin/env bash
#
# Remove a user install made by install.sh.
#
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"

rm -rf "$PREFIX/share/keyforge"
rm -f  "$PREFIX/bin/keyforge"
rm -f  "$PREFIX/share/applications/keyforge.desktop"

echo "Removed $PREFIX/share/keyforge, the keyforge launcher and the desktop entry."
echo "The udev rule, if you installed one, is still in /etc/udev/rules.d/99-keyforge.rules."
