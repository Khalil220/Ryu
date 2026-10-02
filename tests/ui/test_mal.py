import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request

from pywinauto import Application
from pywinauto.keyboard import send_keys

from test_ui import check, spoken, wait_for
import test_ui

CALLBACK = "http://localhost:47813/callback"


class FakeMal(http.server.BaseHTTPRequestHandler):
    token_requests = []

    def reply(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        form = urllib.parse.parse_qs(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode())
        if self.path != "/v1/oauth2/token":
            self.reply(404, {"error": "not_found"})
            return
        FakeMal.token_requests.append(form)
        if form.get("code") == ["good-code"]:
            self.reply(200, {"token_type": "Bearer", "expires_in": 3600, "access_token": "access-1",
                             "refresh_token": "refresh-1"})
        else:
            self.reply(400, {"error": "invalid_request", "message": "The code is wrong."})

    def do_GET(self):
        if self.path == "/v2/users/@me" and self.headers.get("Authorization") == "Bearer access-1":
            self.reply(200, {"id": 1, "name": "tester"})
        else:
            self.reply(401, {"error": "invalid_token"})

    def log_message(self, *args):
        pass


def launch(exe, env):
    process = subprocess.Popen([exe], env=env)
    app = Application(backend="uia").connect(process=process.pid, timeout=20)
    app.window(title_re="Ryu.*").wait("visible", timeout=20)
    return process, app.window(handle=app.window(title_re="Ryu.*").handle)


def open_preferences(main):
    main.set_focus()
    send_keys("^p", vk_packet=False)
    preferences = main.child_window(title="Preferences", control_type="Window")
    wait_for(preferences.exists, 10)
    return preferences


def press(button):
    threading.Thread(target=button.invoke, daemon=True).start()


def mal_button(preferences, name):
    return preferences.child_window(title=name, control_type="Button")


def mal_status(preferences):
    return [text.window_text() for text in preferences.descendants(control_type="Text")
            if "ogged in" in text.window_text()]


def opened_urls(path):
    try:
        with open(path, encoding="utf-8") as f:
            return [line.strip() for line in f if line.strip()]
    except FileNotFoundError:
        return []


def start_login(main, preferences, urls_file):
    before = len(opened_urls(urls_file))
    press(mal_button(preferences, "Log in"))
    wait_for(lambda: len(opened_urls(urls_file)) > before, 10)
    urls = opened_urls(urls_file)
    waiting = main.child_window(title="MyAnimeList login", control_type="Window")
    wait_for(waiting.exists, 10)
    return (urls[-1] if len(urls) > before else ""), waiting


def browse(url):
    with urllib.request.urlopen(url, timeout=10) as response:
        return response.read().decode()


def run(exe, root):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeMal)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    settings = os.path.join(root, "settings.ini")
    speech_log = os.path.join(root, "speech.log")
    urls_file = os.path.join(root, "opened.txt")
    env = dict(os.environ, RYU_CONFIG_FILE=settings, RYU_MPV_OPTIONS="ao=null", RYU_LOG=os.path.join(root, "ryu.log"),
               RYU_SPEECH_LOG=speech_log, RYU_UPDATE_FEED="http://127.0.0.1:9/none",
               RYU_MAL_SERVER=f"http://127.0.0.1:{server.server_address[1]}", RYU_OPENED_URLS=urls_file)
    process, main = launch(exe, env)
    try:
        preferences = open_preferences(main)
        check(mal_button(preferences, "Log in").exists() and mal_status(preferences) == ["Not logged in"],
              f"Preferences offers to log in when nobody is logged in ({mal_status(preferences)})")
        groups = [box.window_text() for box in preferences.descendants(control_type="Group")]
        check(groups == ["Sources", "Playback", "Appearance", "Updates", "MyAnimeList"],
              f"Preferences is split into named groups ({groups})")
        check(mal_button(preferences, "Log in").legacy_properties().get("Description") == "Not logged in",
              "the Log in button's description says nobody is logged in")

        url, waiting = start_login(main, preferences, urls_file)
        query = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)
        check(url.startswith(env["RYU_MAL_SERVER"] + "/v1/oauth2/authorize?") and
              query.get("response_type") == ["code"] and query.get("redirect_uri") == [CALLBACK] and
              query.get("code_challenge_method") == ["plain"] and len(query.get("code_challenge", [""])[0]) >= 43 and
              query.get("client_id") == ["7b530d8dae27b90f2e328b462f335ce0"],
              f"logging in opens MyAnimeList's approval page with Ryu's client and challenge ({url[:120]})")
        check(waiting.exists() and any("Approve Ryu in your browser" in text.window_text()
                                       for text in waiting.descendants(control_type="Text")),
              "a dialog says Ryu is waiting for the browser")
        waiting.child_window(title="Cancel", control_type="Button").invoke()
        check(wait_for(lambda: not waiting.exists(), 10) is not None and mal_button(preferences, "Log in").exists(),
              "cancelling the wait leaves Ryu logged out")
        time.sleep(1)

        url, waiting = start_login(main, preferences, urls_file)
        state = urllib.parse.parse_qs(urllib.parse.urlparse(url).query).get("state", [""])[0]
        page = browse(f"{CALLBACK}?error=access_denied&state={urllib.parse.quote(state)}")
        check("was not logged in" in page, f"the browser is told when MyAnimeList refused ({page[-90:]})")
        refused = main.child_window(title="Could not log in", control_type="Window")
        check(wait_for(refused.exists, 10) is not None and
              any("did not approve" in text.window_text() for text in refused.descendants(control_type="Text")),
              "Ryu says MyAnimeList did not approve the login")
        if refused.exists():
            refused.child_window(title="OK", control_type="Button").invoke()
        time.sleep(1)

        url, waiting = start_login(main, preferences, urls_file)
        query = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)
        state = query.get("state", [""])[0]
        page = browse(f"{CALLBACK}?code=good-code&state={urllib.parse.quote(state)}")
        check("go back to Ryu" in page, f"the browser is told to go back to Ryu ({page[-70:]})")
        check(wait_for(mal_button(preferences, "Log out").exists, 15) is not None and
              mal_status(preferences) == ["Logged in as tester"],
              f"after approval the button offers to log out, next to the account's name ({mal_status(preferences)})")
        check(mal_button(preferences, "Log out").legacy_properties().get("Description") == "Logged in as tester",
              "the Log out button's description names the account")
        check(not waiting.exists(), "the waiting dialog closes by itself")
        exchange = FakeMal.token_requests[-1] if FakeMal.token_requests else {}
        check(exchange.get("grant_type") == ["authorization_code"] and
              exchange.get("code_verifier") == query.get("code_challenge") and
              exchange.get("redirect_uri") == [CALLBACK] and "client_secret" not in exchange,
              f"the code is exchanged with the same verifier and no secret ({sorted(exchange)})")
        check(not spoken(speech_log), f"nothing is announced when the login lands ({spoken(speech_log)})")

        def saved():
            try:
                with open(settings, encoding="utf-8") as f:
                    return f.read()
            except FileNotFoundError:
                return ""

        check(wait_for(lambda: "User=tester" in saved(), 10) is not None, "the account is saved straight away")
        check("access-1" not in saved() and "refresh-1" not in saved() and "RefreshToken=" in saved(),
              "the saved tokens are not readable in the settings file")
        preferences.child_window(title="Cancel", control_type="Button").invoke()
        main.close()
        process.wait(15)

        process, main = launch(exe, env)
        preferences = open_preferences(main)
        logout = mal_button(preferences, "Log out")
        check(logout.exists() and mal_status(preferences) == ["Logged in as tester"],
              "the login survives closing Preferences with Cancel and restarting Ryu")
        if logout.exists():
            logout.invoke()
        check(wait_for(mal_button(preferences, "Log in").exists, 5) is not None,
              "logging out turns the button back into Log in")
        check(wait_for(lambda: "MyAnimeList" not in saved(), 10) is not None,
              "logging out removes the account from the settings file")
        preferences.child_window(title="OK", control_type="Button").invoke()
        main.close()
        process.wait(15)
    finally:
        server.shutdown()
        if process.poll() is None:
            process.kill()


def main(exe):
    root = tempfile.mkdtemp(prefix="ryu-mal-ui-")
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
