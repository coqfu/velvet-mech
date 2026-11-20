"""Entry point for the frozen Windows build.

A windowed PyInstaller app has no console, so an import-time failure is
invisible: by default it becomes a modal message box, which in a headless run
looks exactly like a hang.  This launcher therefore

  * writes a boot marker before importing anything else,
  * records a full traceback in crash.log if the import or main() raises,
  * truncates both files at start, so a stale traceback cannot be mistaken for
    a fresh one.

Both live in %LOCALAPPDATA%\\KeyForge\\ next to server.py's own keyforge.log.
No boot marker at all means the crash happened in the PyInstaller bootloader,
before Python started - that is a different bug from an import that failed.
"""

import os
import sys
import time
import traceback

BASE = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
DIR = os.path.join(BASE, "KeyForge")
BOOT = os.path.join(DIR, "boot.log")
CRASH = os.path.join(DIR, "crash.log")


def _write(path, text, mode):
    try:
        os.makedirs(DIR, exist_ok=True)
        with open(path, mode, encoding="utf-8") as f:
            f.write(text)
            f.flush()
    except OSError:
        pass
    try:
        if sys.stderr is not None:
            sys.stderr.write(text)
            sys.stderr.flush()
    except (OSError, ValueError):
        pass


def _stamp(message):
    _write(BOOT, "%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), message), "a")


def run():
    _write(BOOT, "", "w")
    _write(CRASH, "", "w")
    _stamp("boot python=%s argv=%r exe=%s"
           % (sys.version.split()[0], sys.argv[1:], sys.executable))
    _stamp("frozen=%s meipass=%s" % (getattr(sys, "frozen", False),
                                     getattr(sys, "_MEIPASS", None)))
    from server import main
    _stamp("imported server")
    code = main()
    _stamp("exit %r" % (code,))
    return code


if __name__ == "__main__":
    try:
        sys.exit(run() or 0)
    except SystemExit:
        raise
    except BaseException:
        _write(CRASH, "%s\n%s" % (time.strftime("%Y-%m-%d %H:%M:%S"),
                                  traceback.format_exc()), "a")
        raise
