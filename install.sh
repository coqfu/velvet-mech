#!/usr/bin/env bash
#
# Install KeyForge for the current user.  Nothing outside $HOME is touched
# unless you pass --udev, which installs the hidraw permission rule (the app
# needs it to talk to the keyboard without root).
#
#   ./install.sh              install to ~/.local
#   ./install.sh --udev       ... and install /etc/udev/rules.d/99-keyforge.rules
#   PREFIX=/usr/local ./install.sh
#
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
APP_DIR="$PREFIX/share/keyforge"
BIN_DIR="$PREFIX/bin"
DESKTOP_DIR="$PREFIX/share/applications"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEX="$ROOT/build/keyboard.hex"

if [ ! -f "$HEX" ]; then
    echo "build/keyboard.hex is missing. Run 'make keyboard' first." >&2
    exit 1
fi

if ! command -v python3 >/dev/null; then
    echo "python3 is required." >&2
    exit 1
fi

HIDAPI_LIBS="$(ldconfig -p 2>/dev/null | grep -i hidapi || true)"
if [ -z "$HIDAPI_LIBS" ]; then
    echo "warning: libhidapi was not found. Install hidapi (Arch: hidapi," \
         "Debian: libhidapi-hidraw0) or the app will refuse to start." >&2
fi

echo "Installing to $APP_DIR"
mkdir -p "$APP_DIR" "$BIN_DIR" "$DESKTOP_DIR"

# The app, its shared data and the firmware image it flashes.  Sources are
# copied too so the installed tree can rebuild the firmware in place.
tar -C "$ROOT" -cf - \
    --exclude='app/ui/node_modules' \
    --exclude='app/ui/dist.prev' \
    --exclude='app/ui/dist.next' \
    --exclude='__pycache__' \
    --exclude='host/*.log' \
    --exclude='tools/wchisp' \
    app host build include src tools Makefile README.md LICENSE docs \
    | tar -C "$APP_DIR" -xf -

cat > "$BIN_DIR/keyforge" <<LAUNCHER
#!/usr/bin/env bash
exec python3 "$APP_DIR/app/server.py" "\$@"
LAUNCHER
chmod +x "$BIN_DIR/keyforge"

sed "s|@APP_DIR@|$APP_DIR|g" "$ROOT/packaging/keyforge.desktop" \
    > "$DESKTOP_DIR/keyforge.desktop"

echo "Launcher:  $BIN_DIR/keyforge"
echo "Desktop:   $DESKTOP_DIR/keyforge.desktop"

if [ "${1:-}" = "--udev" ]; then
    echo "Installing the udev rule (needs sudo)..."
    sudo install -m 644 "$ROOT/packaging/99-keyforge.rules" /etc/udev/rules.d/99-keyforge.rules
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "Rule installed: /etc/udev/rules.d/99-keyforge.rules"
else
    echo
    echo "If 'keyforge' reports that the keyboard cannot be opened, install"
    echo "the permission rule with:  sudo install -m 644 packaging/99-keyforge.rules \\"
    echo "  /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger"
fi

echo
echo "Run it with:  keyforge"
