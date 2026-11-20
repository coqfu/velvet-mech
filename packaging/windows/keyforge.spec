# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for KeyForge.exe (Windows x64).

    python -m PyInstaller --noconfirm --clean packaging/windows/keyforge.spec \
        --distpath dist/windows --workpath build/pyinstaller-windows

The result is one console-less file, dist/windows/KeyForge.exe, that holds the
service, the built UI, the firmware image it flashes and hidapi.dll.  Use
packaging/windows/build.ps1 (or the CI workflow) rather than calling this
directly: the DLL has to be fetched first.

Start-up failures are invisible in a windowed build, so server.py writes them
to %LOCALAPPDATA%\\KeyForge\\keyforge.log and to a message box.
"""

import os

ROOT = os.path.abspath(os.path.join(SPECPATH, "..", ".."))
APP = os.path.join(ROOT, "app")


def need(path):
    if not os.path.exists(path):
        raise SystemExit("missing build input: %s" % path)
    return path


# libusb/hidapi's own Windows release: hidapi.dll built against the Windows
# HID API (no WinUSB driver needed for the keyboard itself).
hidapi = need(os.path.join(SPECPATH, "hidapi.dll"))

# Only the markdown docs: docs/ also holds the WCH datasheet when the
# developer has downloaded it, and that must not be redistributed.
docs_dir = os.path.join(ROOT, "docs")
doc_files = [(os.path.join(docs_dir, name), "docs")
             for name in sorted(os.listdir(docs_dir)) if name.endswith(".md")]

datas = [
    (need(os.path.join(APP, "ui", "dist")), os.path.join("app", "ui", "dist")),
    (need(os.path.join(ROOT, "build", "keyboard.hex")), "build"),
    (need(os.path.join(ROOT, "host", "map.json")), "host"),
    (need(os.path.join(ROOT, "README.md")), "."),
    (need(os.path.join(ROOT, "LICENSE")), "."),
] + doc_files

a = Analysis(
    [os.path.join(APP, "server.py")],
    pathex=[APP],
    binaries=[(hidapi, ".")],
    datas=datas,
    hiddenimports=["soundcard"],
    hookspath=[],
    runtime_hooks=[],
    excludes=["tkinter", "unittest", "pydoc_data"],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="KeyForge",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
