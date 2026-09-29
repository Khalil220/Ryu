import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import tempfile
import threading
import time

from pywinauto import Application
from pywinauto.keyboard import send_keys

from test_ui import check, items, wait_for
import test_ui

user32 = ctypes.windll.user32
user32.SendMessageTimeoutW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM, wt.UINT, wt.UINT,
                                       ctypes.POINTER(ctypes.c_size_t)]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]

LVM_GETITEMCOUNT = 0x1004
LONGEST_ACCEPTABLE_STALL = 0.25


def watch(hwnd, stop, gaps):
    result = ctypes.c_size_t()
    while not stop.is_set():
        start = time.perf_counter()
        user32.SendMessageTimeoutW(hwnd, 0, 0, 0, 0x0002, 30000, ctypes.byref(result))
        gaps.append((start, time.perf_counter() - start))
        time.sleep(0.02)


def worst(gaps, since):
    return max((gap for stamp, gap in gaps if stamp >= since), default=0)


def run(app, pid):
    handle = app.window(title_re="Ryu.*").handle
    window = app.window(handle=handle)
    native = Application(backend="win32").connect(process=pid).window(handle=handle)
    episodes_hwnd = [child.handle for child in native.descendants() if child.class_name() == "SysListView32"][1]

    stop = threading.Event()
    gaps = []
    threading.Thread(target=watch, args=(handle, stop, gaps), daemon=True).start()
    try:
        window.child_window(title="Search", control_type="Edit").set_edit_text("one piece")
        window.child_window(title="Search", control_type="Button").invoke()
        results = window.child_window(title="Results", control_type="List")
        first = wait_for(lambda: items(results)[0])
        check(first is not None and first.window_text().startswith("One Piece, TV"),
              f"One Piece is the first result ({first.window_text() if first else None})")
        if first is None:
            return
        results.set_focus()
        first.select()
        time.sleep(1)

        started = time.perf_counter()
        send_keys("{ENTER}")
        count = wait_for(lambda: user32.SendMessageW(episodes_hwnd, LVM_GETITEMCOUNT, 0, 0) > 1000 or None, 60)
        time.sleep(0.5)
        loaded = user32.SendMessageW(episodes_hwnd, LVM_GETITEMCOUNT, 0, 0)
        stall = worst(gaps, started)
        check(count is not None, f"over a thousand episodes load ({loaded})")
        check(stall < LONGEST_ACCEPTABLE_STALL, f"loading them never freezes the window for long ({stall * 1000:.0f} ms)")

        audio = window.child_window(title="Audio", control_type="ComboBox")
        audio.set_focus()
        changed = time.perf_counter()
        send_keys("{DOWN}")
        time.sleep(1)
        stall = worst(gaps, changed)
        check(stall < LONGEST_ACCEPTABLE_STALL, f"changing the audio right after stays responsive ({stall * 1000:.0f} ms)")
        send_keys("{UP}")
    finally:
        stop.set()
    window.close()


def main(exe):
    config = os.path.join(tempfile.gettempdir(), f"ryu-responsiveness-{os.getpid()}.ini")
    process = subprocess.Popen([os.path.abspath(exe)], env=dict(os.environ, RYU_CONFIG_FILE=config))
    try:
        app = Application(backend="uia").connect(process=process.pid, timeout=20)
        app.window(title_re="Ryu.*").wait("visible", timeout=20)
        run(app, process.pid)
        process.wait(15)
    except Exception as error:
        check(False, f"unexpected error: {error!r}")
    finally:
        if process.poll() is None:
            process.kill()
        if os.path.exists(config):
            os.remove(config)
    print(f"{test_ui.failures} failure(s)")
    return 1 if test_ui.failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
