import functools
import hashlib
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import zipfile

from pywinauto import Application, Desktop
from pywinauto.keyboard import send_keys

from test_ui import check, wait_for
import test_ui

PACKAGE = "Ryu-9.9.9-win64.zip"
PACKAGE_SECONDS = 3
PACKAGE_CHUNKS = 30


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    requested = []

    def do_GET(self):
        QuietHandler.requested.append(self.path)
        if self.path.endswith(PACKAGE):
            self.send_slowly()
        else:
            super().do_GET()

    def send_slowly(self):
        with open(os.path.join(self.directory, PACKAGE), "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", "application/zip")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        step = -(-len(data) // PACKAGE_CHUNKS)
        try:
            for start in range(0, len(data), step):
                self.wfile.write(data[start:start + step])
                self.wfile.flush()
                time.sleep(PACKAGE_SECONDS / PACKAGE_CHUNKS)
        except OSError:
            pass

    def log_message(self, *args):
        pass


def publish(folder, port, version):
    feed = {
        "tag_name": f"v{version}",
        "html_url": f"http://127.0.0.1:{port}/release",
        "body": "- A test release",
        "assets": [
            {"name": PACKAGE, "browser_download_url": f"http://127.0.0.1:{port}/{PACKAGE}"},
            {"name": "SHA256SUMS", "browser_download_url": f"http://127.0.0.1:{port}/SHA256SUMS"},
        ],
    }
    with open(os.path.join(folder, "feed.json"), "w", encoding="utf-8") as f:
        json.dump(feed, f)


def dialog_text(dialog):
    return " ".join(text.window_text() for text in dialog.descendants(control_type="Text"))


def answer(main, title, button):
    dialog = main.child_window(title=title, control_type="Window")
    if wait_for(dialog.exists, 60) is None:
        return None
    text = dialog_text(dialog)
    dialog.child_window(title_re=f"&?{button}", control_type="Button").invoke()
    return text


def progress_value(dialog):
    bar = dialog.child_window(control_type="ProgressBar")
    return bar.iface_range_value.CurrentValue if bar.exists() else None


def cancel_download(main, app_folder, log):
    before = sorted(os.listdir(app_folder))
    dialog = main.child_window(title="Updating Ryu", control_type="Window")
    if wait_for(dialog.exists, 20) is None:
        check(False, "a progress dialog shows while the update downloads")
        return
    text = dialog_text(dialog)
    check("Downloading Ryu 9.9.9" in text, f"a progress dialog shows while the update downloads ({text})")
    moved = wait_for(lambda: (progress_value(dialog) or 0) > 0, 10)
    check(moved is not None, f"its progress bar moves as the package arrives ({moved})")
    dialog.child_window(title="Cancel", control_type="Button").invoke()
    check(wait_for(lambda: not dialog.exists(), 10) is not None, "Cancel closes the progress dialog")
    time.sleep(PACKAGE_SECONDS)
    others = [title for title in ("Update installed", "Update failed")
              if main.child_window(title=title, control_type="Window").exists()]
    check(not others, f"cancelling shows no error or restart question ({others})")
    check(sorted(os.listdir(app_folder)) == before, f"cancelling leaves Ryu's folder as it was ({os.listdir(app_folder)})")
    with open(log, encoding="utf-8", errors="replace") as f:
        lines = f.read()
    check("was cancelled after" in lines and "The update was cancelled" in lines,
          "the log records that the download stopped")


def connect(path):
    def attempt():
        app = Application(backend="uia").connect(path=path, timeout=1)
        return app if app.window(title_re="Ryu.*").exists() else None
    return wait_for(attempt, 30)


def locked_folder(source, root, env):
    folder = os.path.join(root, "locked")
    os.makedirs(folder)
    for name in ("ryu.exe", "libmpv-2.dll", "prism.dll"):
        shutil.copy2(os.path.join(source, name), folder)
    subprocess.run(["icacls", folder, "/deny", "*S-1-1-0:(WD,AD)"], check=True, capture_output=True)
    process = subprocess.Popen([os.path.join(folder, "ryu.exe")], env=env)
    try:
        QuietHandler.requested.clear()
        app = Application(backend="uia").connect(process=process.pid, timeout=20)
        app.window(title_re="Ryu.*").wait("visible", timeout=20)
        main = app.window(handle=app.window(title_re="Ryu.*").handle)
        notice = answer(main, "Update available", "No")
        check(notice is not None and "needs administrator rights" in notice,
              f"a Ryu in a protected folder explains why it can't update ({notice})")
        check(not any(path.endswith(PACKAGE) for path in QuietHandler.requested),
              f"a Ryu in a protected folder downloads nothing ({QuietHandler.requested})")
        check(sorted(os.listdir(folder)) == ["libmpv-2.dll", "prism.dll", "ryu.exe"],
              f"the protected folder is left untouched ({os.listdir(folder)})")
        main.close()
        process.wait(15)
    finally:
        if process.poll() is None:
            process.kill()
        subprocess.run(["icacls", folder, "/remove:d", "*S-1-1-0"], capture_output=True)


def run(exe, root):
    app_folder = os.path.join(root, "app")
    served = os.path.join(root, "served")
    os.makedirs(app_folder)
    os.makedirs(served)
    source = os.path.dirname(os.path.abspath(exe))
    for name in ("ryu.exe", "libmpv-2.dll", "prism.dll"):
        shutil.copy2(os.path.join(source, name), app_folder)
    package = os.path.join(served, PACKAGE)
    with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(os.path.join(source, "ryu.exe"), "ryu.exe")
        archive.write(os.path.join(source, "prism.dll"), "prism.dll")
    with open(package, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    with open(os.path.join(served, "SHA256SUMS"), "w", encoding="ascii") as f:
        f.write(f"{digest}  {PACKAGE}\n")

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=served))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    publish(served, port, "9.9.9")

    log = os.path.join(root, "ryu.log")
    env = dict(os.environ, RYU_CONFIG_FILE=os.path.join(root, "settings.ini"), RYU_MPV_OPTIONS="ao=null",
               RYU_LOG=log, RYU_SPEECH_LOG=os.path.join(root, "speech.log"),
               RYU_UPDATE_FEED=f"http://127.0.0.1:{port}/feed.json")
    program = os.path.join(app_folder, "ryu.exe")
    try:
        locked_folder(source, root, env)
    except Exception:
        server.shutdown()
        raise
    first = subprocess.Popen([program], env=env)
    restarted = None
    try:
        app = Application(backend="uia").connect(process=first.pid, timeout=20)
        app.window(title_re="Ryu.*").wait("visible", timeout=20)
        main = app.window(handle=app.window(title_re="Ryu.*").handle)

        offer = answer(main, "Update available", "Yes")
        check(offer is not None and "Ryu 9.9.9 is available" in offer,
              f"the startup check offers the newer release ({offer})")
        cancel_download(main, app_folder, log)

        main.set_focus()
        send_keys("%h", vk_packet=False)
        send_keys("u", vk_packet=False)
        retry = answer(main, "Update available", "Yes")
        check(retry is not None, "checking by hand offers the cancelled update again")
        check(wait_for(main.child_window(title="Updating Ryu", control_type="Window").exists, 20) is not None,
              "the progress dialog shows again for the second attempt")
        installed = answer(main, "Update installed", "Yes")
        check(installed is not None and "Restart Ryu now" in installed,
              f"Ryu asks to restart once the update is in place ({installed})")
        check(os.path.exists(os.path.join(app_folder, "ryu.exe.ryu-old")),
              "the running program was set aside instead of overwritten")
        check(os.path.exists(os.path.join(app_folder, "libmpv-2.dll")) and
              not os.path.exists(os.path.join(app_folder, "libmpv-2.dll.ryu-old")),
              "files missing from the package are left alone")
        check(not os.path.exists(os.path.join(app_folder, ".ryu-update")), "the unpacked files are cleaned up")
        try:
            first.wait(15)
            check(True, "restarting closes the old Ryu")
        except subprocess.TimeoutExpired:
            check(False, "restarting closes the old Ryu")

        restarted = connect(program)
        check(restarted is not None, "restarting starts the updated Ryu")
        if restarted is None:
            return
        main = restarted.window(handle=restarted.window(title_re="Ryu.*").handle)
        again = answer(main, "Update available", "No")
        check(again is not None, "the restarted Ryu checks again on startup")
        check(wait_for(lambda: not os.path.exists(os.path.join(app_folder, "ryu.exe.ryu-old")), 15) is not None,
              "the set-aside files are removed once the old Ryu has exited")

        publish(served, port, "0.0.1")
        main.set_focus()
        send_keys("%h", vk_packet=False)
        send_keys("u", vk_packet=False)
        latest = answer(main, "Check for updates", "OK")
        check(latest is not None and "You have the latest version of Ryu" in latest,
              f"checking by hand says when Ryu is up to date ({latest})")

        send_keys("^p", vk_packet=False)
        preferences = main.child_window(title="Preferences", control_type="Window")
        if wait_for(preferences.exists, 10) is not None:
            box = preferences.child_window(title="Check for updates when Ryu starts", control_type="CheckBox")
            check(box.exists() and box.get_toggle_state() == 1, "Preferences offers the startup check, on by default")
            preferences.child_window(title="Cancel", control_type="Button").invoke()
        main.close()
    finally:
        server.shutdown()
        if first.poll() is None:
            first.kill()
        if restarted is not None:
            try:
                restarted.kill()
            except Exception:
                pass


def main(exe):
    root = tempfile.mkdtemp(prefix="ryu-update-ui-")
    try:
        run(exe, root)
    except Exception as error:
        check(False, f"unexpected error: {error!r}")
    finally:
        for _ in range(20):
            try:
                shutil.rmtree(root)
                break
            except OSError:
                time.sleep(0.5)
    print(f"{test_ui.failures} failure(s)")
    return 1 if test_ui.failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
