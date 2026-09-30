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

from test_ui import check, spoken, wait_for
import test_ui

PACKAGE = "Ryu-9.9.9-win64.zip"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
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


def connect(path):
    def attempt():
        app = Application(backend="uia").connect(path=path, timeout=1)
        return app if app.window(title_re="Ryu.*").exists() else None
    return wait_for(attempt, 30)


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

    speech_log = os.path.join(root, "speech.log")
    env = dict(os.environ, RYU_CONFIG_FILE=os.path.join(root, "settings.ini"), RYU_MPV_OPTIONS="ao=null",
               RYU_LOG=os.path.join(root, "ryu.log"), RYU_SPEECH_LOG=speech_log,
               RYU_UPDATE_FEED=f"http://127.0.0.1:{port}/feed.json")
    program = os.path.join(app_folder, "ryu.exe")
    first = subprocess.Popen([program], env=env)
    restarted = None
    try:
        app = Application(backend="uia").connect(process=first.pid, timeout=20)
        app.window(title_re="Ryu.*").wait("visible", timeout=20)
        main = app.window(handle=app.window(title_re="Ryu.*").handle)

        offer = answer(main, "Update available", "Yes")
        check(offer is not None and "Ryu 9.9.9 is available" in offer,
              f"the startup check offers the newer release ({offer})")
        check(wait_for(lambda: "Downloading the update" in spoken(speech_log), 10) is not None,
              "the download is announced")
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
