# Windows

KeyForge runs on Windows as one file, `KeyForge.exe`. It is the same Python
service and the same React UI as the Linux install, bundled with PyInstaller so
no Python, Node or pip install is needed on the target machine.

**Hardware is unchanged: the iGear Raptor (CH555, 63 keys) only.** Windows does
not make the firmware portable; see "Porting" in `docs/INSTALL.md`.

## What is verified, and what is not

Be precise about this, because a Windows build cannot be produced on the Linux
development machine:

| part | state |
|---|---|
| the exe builds, and so do the installer and the portable zip | verified by CI on `windows-latest` (`.github/workflows/windows-exe.yml`) |
| the exe starts and serves the UI | verified by the same CI job: `/api/status` answers and `/` returns the built page |
| the installer installs, runs and uninstalls | verified by the same CI job: silent install, `--selftest` from the installed copy, then a silent uninstall that leaves no exe behind |
| USB keyboard access (lighting, effects, keymap, macros, profiles, diagnostics) | same hidapi calls that are verified on Linux; not yet exercised against the board on Windows |
| firmware flashing | `wchisp.exe` plus the WinUSB driver, described below; not yet run on Windows |
| Sound page | WASAPI loopback through SoundCard; the capture code is exercised on Linux through the same class, but not on Windows audio hardware |

If something on Windows disagrees with the above, the log is the first place to
look: `%LOCALAPPDATA%\KeyForge\keyforge.log`.

## Install it

Download **`KeyForge-Setup-<version>.exe`** from the latest release,
<https://github.com/coqfu/velvet-mech/releases/latest>, and run it.

It is a per-user install: no admin prompt, no Program Files, no system-wide
registry entries. It goes to `%LOCALAPPDATA%\Programs\KeyForge`, adds a Start
Menu entry and an optional desktop shortcut, and registers itself in Windows'
Apps list so it can be removed normally.

The bundle is complete. Python, numpy, SoundCard, `hidapi.dll` and `wchisp.exe`
are all inside; the target machine needs no Python, no pip packages, no Rust and
no Node. The only thing it cannot carry is the WinUSB driver that `wchisp` needs
for the bootloader, because that is a signed kernel driver installed per machine
(see "Flashing firmware" below).

Silent install, if you script it:

```powershell
.\KeyForge-Setup-<version>.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

The build is unsigned, so SmartScreen shows "Windows protected your PC" the
first time - both for the setup and for the portable exe. Choose
**More info → Run anyway**. There is no certificate behind this project, and
saying otherwise would be misleading.

### Portable instead

`KeyForge-<version>-portable.zip` holds the same files unpacked:
`KeyForge.exe`, `wchisp.exe`, `README.md`, `LICENSE` and `docs/WINDOWS.md`.
`wchisp.exe` is redundant there (the exe carries its own copy) but harmless.
Unzip anywhere - including a USB stick - and run `KeyForge.exe`. State still
goes to `%LOCALAPPDATA%\KeyForge`, not next to the exe.

### Build it yourself

You need Windows x64, Python 3.10 or newer, and a network connection (the build
fetches `hidapi.dll` from libusb/hidapi, `wchisp.exe` from ch32-rs/wchisp, and
PyInstaller from PyPI):

```powershell
git clone https://github.com/coqfu/velvet-mech.git
cd velvet-mech
powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1
```

That produces three files in `dist\windows\`:

| file | what |
|---|---|
| `KeyForge.exe` | single-file app, ~24 MB, carries `wchisp.exe` |
| `KeyForge-Setup-<v>.exe` | the installer above (needs Inno Setup; the script installs it via Chocolatey if missing) |
| `KeyForge-<v>-portable.zip` | exe + `wchisp.exe` + docs |

`-NoClean` skips the PyInstaller cache reset; `-SkipInstaller` stops after the
bare exe; `-Version 1.0.1` changes the names.

PyInstaller cannot cross-compile, which is why the release binaries are built by
the GitHub Actions workflow on a Windows runner rather than on Linux.

## Run it

Double-click `KeyForge.exe`, or from a shell:

```powershell
KeyForge.exe                  # serves http://127.0.0.1:8765/ and opens the browser
KeyForge.exe --no-browser     # just serve
KeyForge.exe --port 8800      # different port
KeyForge.exe --selftest       # check hidapi, the bundled UI, key map and data dir
```

The installer does not add anything to `PATH`: start it from the Start Menu, or
run it by full path from `%LOCALAPPDATA%\Programs\KeyForge`. `--selftest` writes
one line per check to the log and exits non-zero if a required piece is missing,
which is the fastest way to tell a broken bundle from a disconnected keyboard.

It binds `127.0.0.1` only, so nothing is reachable from the network. Windows
Firewall has nothing to ask about for the same reason.

The window is deliberately absent: the UI is in your browser, exactly as on
Linux. If start-up fails (hidapi missing, port taken, no browser) a message box
says so and the same text goes to `%LOCALAPPDATA%\KeyForge\keyforge.log`.

**State** lives in `%LOCALAPPDATA%\KeyForge\`:

| file | what |
|---|---|
| `profiles.json` | saved lighting/keyset profiles |
| `keyforge.log` | start-up and shutdown lines |

Delete that folder and the exe to uninstall; nothing is written to the registry
or to Program Files.

## Flashing firmware on Windows

The board's ROM loader is entered by the **boot jumper** and a power cycle - the
same as on Linux, and there is still no software route in
(`bBOOT_LOAD` is read-only and cleared by a software reset).

One Windows-specific piece:

**The WinUSB driver for `4348:55e0`.** `wchisp` talks to the loader through
libusb, which on Windows needs a WinUSB driver bound to the bootloader device.
Use [Zadig](https://zadig.akeo.ie/): with the board in bootloader mode
(`4348:55e0`), select that device and install **WinUSB**. This is a one-time step
per machine, and it does not affect the keyboard mode (`1209:c555`), which uses
the normal Windows HID driver.

`wchisp.exe` ships with the app - it is inside `KeyForge.exe` itself and also
sits next to it in the install directory, so even the single portable file can
flash from any folder with nothing installed. The app looks in its own bundle
first, then beside the exe, then on `PATH`, then in `%USERPROFILE%\.cargo\bin`.
If you prefer your own build, `cargo install wchisp` works and is found on
`PATH`.

Then the guided flash in **Advanced** works as described in
`docs/INSTALL.md`: press Flash, fit the jumper, unplug and replug. The job waits
for the loader (it polls `wchisp probe`), writes `keyboard.hex`, and reports
`Verify OK`; remove the jumper and replug to come back as `Velvet Mech`.

If the job says the bootloader never appeared, the two usual causes are the
jumper fitted *after* the power cycle instead of before, and a missing WinUSB
driver (the app adds that hint to the failure message on Windows).

## Sound page

Windows has no PulseAudio monitor, so the Sound page records **WASAPI loopback**
of the default speaker through the bundled [SoundCard](https://github.com/bastibe/SoundCard)
package. Pick the mode and palette, press Start, play something, and the board
runs Direct while the host streams frames. Stop restores the effect you had.

Two honest caveats: this path has not been run against Windows audio hardware
in this project, and it records the *default* output device - switch the
default speaker in Windows if the wrong one is playing.

## OpenRGB

The board speaks QMK/OpenRGB protocol D on usage page `0xFF60`, usage `0x61`.
On Windows, OpenRGB must be started **after** closing KeyForge: one process owns
the raw HID handle at a time, and KeyForge says "connected: false" while
another program holds it. This is the same rule as on Linux.

## Differences from the Linux install

| | Linux | Windows |
|---|---|---|
| entry point | `keyforge` launcher in `~/.local/bin` | Start Menu shortcut (installer) or `KeyForge.exe` |
| dependencies | python3, numpy, hidapi, wchisp | none - all bundled |
| permissions | udev rule for `1209:c555` | none needed (HID devices are user-accessible) |
| audio capture | `parec` monitor source | WASAPI loopback via SoundCard |
| flasher | `wchisp` on `PATH` | `wchisp.exe` + WinUSB driver via Zadig |
| state | `app/profiles.json` in the install tree | `%LOCALAPPDATA%\KeyForge\profiles.json` |
| firmware | identical | identical - it is the board, not the host |

## Troubleshooting

| symptom | cause |
|---|---|
| message box: "hidapi not found" | `hidapi.dll` was not next to the exe; rebuild, or copy it beside `KeyForge.exe` |
| message box: "could not start on 127.0.0.1:8765" | another KeyForge instance is running (or the port is taken); use `--port` |
| double-click does nothing | look at `%LOCALAPPDATA%\KeyForge\keyforge.log`; a SmartScreen block shows nothing in the log |
| page loads, "connected: false" | cable/port, another program holding the device (OpenRGB), or the board is in bootloader mode |
| flash job never finds the loader | jumper not fitted before the power cycle, or WinUSB driver missing (Zadig) |
| Sound page: "no loopback device" | no output device selected in Windows, or all outputs disabled |
| text in the UI shows boxes | a browser font fallback; the UI bundles Inter and JetBrains Mono, so this points at a modified `dist` |
