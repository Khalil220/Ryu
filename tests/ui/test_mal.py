import copy
import http.server
import json
import os
import re
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

from test_ui import check, items, spoken, wait_for
import test_ui

CALLBACK = "http://localhost:47813/callback"
CATALOGUE = {
    52991: {"id": 52991, "title": "Sousou no Frieren",
            "alternative_titles": {"synonyms": [], "en": "Frieren: Beyond Journey's End", "ja": ""},
            "media_type": "tv", "num_episodes": 28, "start_season": {"year": 2023, "season": "fall"}},
    59978: {"id": 59978, "title": "Sousou no Frieren 2nd Season",
            "alternative_titles": {"synonyms": [], "en": "Frieren: Beyond Journey's End Season 2", "ja": ""},
            "media_type": "tv", "num_episodes": 10, "start_season": {"year": 2026, "season": "winter"}},
    21: {"id": 21, "title": "One Piece", "alternative_titles": {"synonyms": [], "en": "One Piece", "ja": ""},
         "media_type": "tv", "num_episodes": 0, "start_season": {"year": 1999, "season": "fall"}},
    11061: {"id": 11061, "title": "Hunter x Hunter (2011)",
            "alternative_titles": {"synonyms": ["HxH (2011)"], "en": "Hunter x Hunter", "ja": ""},
            "media_type": "tv", "num_episodes": 148, "start_season": {"year": 2011, "season": "fall"}},
    136: {"id": 136, "title": "Hunter x Hunter",
          "alternative_titles": {"synonyms": ["HxH"], "en": "Hunter x Hunter", "ja": ""},
          "media_type": "tv", "num_episodes": 62, "start_season": {"year": 1999, "season": "fall"}},
    459: {"id": 459, "title": "One Piece Movie 01",
          "alternative_titles": {"synonyms": [], "en": "One Piece: The Movie", "ja": ""},
          "media_type": "movie", "num_episodes": 1, "start_season": {"year": 2000, "season": "winter"}},
}
STARTING_LISTS = {
    52991: {"status": "watching", "score": 0, "num_episodes_watched": 5, "is_rewatching": False},
    21: {"status": "watching", "score": 0, "num_episodes_watched": 1100, "is_rewatching": False},
    59978: {"status": "plan_to_watch", "score": 0, "num_episodes_watched": 0, "is_rewatching": False},
    136: {"status": "completed", "score": 9, "num_episodes_watched": 62, "is_rewatching": False},
}


class FakeMal(http.server.BaseHTTPRequestHandler):
    token_requests = []
    lists = copy.deepcopy(STARTING_LISTS)
    writes = []

    def reply(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def form(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        return {name: values[0] for name, values in urllib.parse.parse_qs(raw, keep_blank_values=True).items()}

    def authorized(self):
        if self.headers.get("Authorization") == "Bearer access-1":
            return True
        self.reply(401, {"error": "invalid_token"})
        return False

    def node(self, anime_id):
        node = dict(CATALOGUE[anime_id])
        if anime_id in FakeMal.lists:
            node["my_list_status"] = FakeMal.lists[anime_id]
        return node

    def list_status_id(self):
        match = re.fullmatch(r"/v2/anime/(\d+)/my_list_status", urllib.parse.urlparse(self.path).path)
        return int(match.group(1)) if match else None

    def do_POST(self):
        form = self.form()
        if self.path != "/v1/oauth2/token":
            self.reply(404, {"error": "not_found"})
            return
        FakeMal.token_requests.append(form)
        if form.get("code") == "good-code":
            self.reply(200, {"token_type": "Bearer", "expires_in": 3600, "access_token": "access-1",
                             "refresh_token": "refresh-1"})
        else:
            self.reply(400, {"error": "invalid_request", "message": "The code is wrong."})

    def do_GET(self):
        if not self.authorized():
            return
        url = urllib.parse.urlparse(self.path)
        query = {name: values[0] for name, values in urllib.parse.parse_qs(url.query).items()}
        if url.path == "/v2/users/@me":
            self.reply(200, {"id": 1, "name": "tester"})
        elif url.path == "/v2/users/@me/animelist":
            wanted = query.get("status")
            self.reply(200, {"data": [{"node": CATALOGUE[anime_id], "list_status": status}
                                      for anime_id, status in FakeMal.lists.items()
                                      if wanted in (None, status["status"])], "paging": {}})
        elif url.path == "/v2/anime":
            text = query.get("q", "").lower()
            self.reply(200, {"data": [{"node": self.node(anime_id)} for anime_id, node in CATALOGUE.items()
                                      if text in node["title"].lower() or
                                      text in node["alternative_titles"]["en"].lower()], "paging": {}})
        elif re.fullmatch(r"/v2/anime/\d+", url.path) and int(url.path.rsplit("/", 1)[1]) in CATALOGUE:
            self.reply(200, self.node(int(url.path.rsplit("/", 1)[1])))
        else:
            self.reply(404, {"error": "not_found"})

    def do_PATCH(self):
        form = self.form()
        anime_id = self.list_status_id()
        if not self.authorized():
            return
        if anime_id not in CATALOGUE:
            self.reply(404, {"error": "not_found"})
            return
        FakeMal.writes.append(("PATCH", anime_id, form))
        status = FakeMal.lists.setdefault(anime_id, {"status": "watching", "score": 0, "num_episodes_watched": 0,
                                                     "is_rewatching": False})
        for name, value in form.items():
            if name == "num_watched_episodes":
                status["num_episodes_watched"] = int(value)
            elif name == "score":
                status["score"] = int(value)
            elif name == "is_rewatching":
                status["is_rewatching"] = value == "true"
            elif value:
                status[name] = value
            else:
                status.pop(name, None)
        self.reply(200, status)

    def do_DELETE(self):
        anime_id = self.list_status_id()
        if not self.authorized():
            return
        FakeMal.writes.append(("DELETE", anime_id, {}))
        self.reply(200 if FakeMal.lists.pop(anime_id, None) else 404, [])

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


def approve(url):
    state = urllib.parse.parse_qs(urllib.parse.urlparse(url).query).get("state", [""])[0]
    return browse(f"{CALLBACK}?code=good-code&state={urllib.parse.quote(state)}")


def texts(window):
    return [text.window_text() for text in window.descendants(control_type="Text")]


def labels(list_box):
    return [item.window_text() for item in items(list_box)]


def login_flow(exe, env, settings, speech_log, urls_file):
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
        check(waiting.exists() and any("Approve Ryu in your browser" in text for text in texts(waiting)),
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
        check(wait_for(refused.exists, 10) is not None and any("did not approve" in text for text in texts(refused)),
              "Ryu says MyAnimeList did not approve the login")
        if refused.exists():
            refused.child_window(title="OK", control_type="Button").invoke()
        time.sleep(1)

        url, waiting = start_login(main, preferences, urls_file)
        query = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)
        page = approve(url)
        check("go back to Ryu" in page, f"the browser is told to go back to Ryu ({page[-70:]})")
        check(wait_for(mal_button(preferences, "Log out").exists, 15) is not None and
              mal_status(preferences) == ["Logged in as tester"],
              f"after approval the button offers to log out, next to the account's name ({mal_status(preferences)})")
        check(mal_button(preferences, "Log out").legacy_properties().get("Description") == "Logged in as tester",
              "the Log out button's description names the account")
        check(not waiting.exists(), "the waiting dialog closes by itself")
        exchange = FakeMal.token_requests[-1] if FakeMal.token_requests else {}
        check(exchange.get("grant_type") == "authorization_code" and
              [exchange.get("code_verifier")] == query.get("code_challenge") and
              exchange.get("redirect_uri") == CALLBACK and "client_secret" not in exchange,
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
        wait_for(lambda: not preferences.exists(), 10)
        main.set_focus()
        send_keys("^l", vk_packet=False)
        time.sleep(1)
        check(not main.child_window(title="List", control_type="ComboBox").exists(),
              "Ctrl+L does nothing while nobody is logged in")
        main.close()
        process.wait(15)
    finally:
        if process.poll() is None:
            process.kill()


def writes_to(anime_id):
    return [(method, form) for method, target, form in FakeMal.writes if target == anime_id]


def open_lists(main):
    main.set_focus()
    send_keys("^l", vk_packet=False)
    chooser = main.child_window(title="List", control_type="ComboBox")
    wait_for(chooser.exists, 10)
    return chooser, main.child_window(title="Anime", control_type="List")


def edit_dialog(main):
    dialog = main.child_window(title_re="(Edit on|Add to) MyAnimeList", control_type="Window")
    wait_for(dialog.exists, 20)
    return dialog


def lists_flow(exe, env, speech_log, urls_file):
    FakeMal.lists = copy.deepcopy(STARTING_LISTS)
    FakeMal.writes.clear()
    process, main = launch(exe, env)
    try:
        preferences = open_preferences(main)
        url, _ = start_login(main, preferences, urls_file)
        approve(url)
        wait_for(mal_button(preferences, "Log out").exists, 15)
        preferences.child_window(title="OK", control_type="Button").invoke()
        wait_for(lambda: not preferences.exists(), 10)

        chooser, anime = open_lists(main)
        check(chooser.exists() and main.window_text() == "My anime lists - Ryu",
              f"Ctrl+L opens the lists page in the same window ({main.window_text()})")
        wanted = ["Frieren: Beyond Journey's End, 5 of 28 episodes", "One Piece, 1100 episodes watched"]
        check(wait_for(lambda: labels(anime) == wanted, 15) is not None and chooser.selected_text() == "Watching",
              f"it starts on Watching, with titles, progress and no extra counts ({labels(anime)})")
        check(wait_for(lambda: anime.has_keyboard_focus() or items(anime)[0].has_keyboard_focus(), 5) is not None,
              "focus lands in the anime list")
        chooser.select("Plan to watch")
        check(wait_for(lambda: labels(anime) == ["Frieren: Beyond Journey's End Season 2, 10 episodes"], 5) is not None,
              f"choosing another list shows its anime ({labels(anime)})")
        chooser.select("Dropped")
        check(wait_for(lambda: labels(anime) == ["Nothing on your Dropped list"], 5) is not None and
              not main.child_window(title="Edit", control_type="Button").exists() and
              not main.child_window(title="Remove", control_type="Button").exists(),
              f"an empty list says so and offers nothing to edit or remove ({labels(anime)})")
        chooser.select("Watching")
        wait_for(lambda: labels(anime) == wanted, 5)

        items(anime)[1].select()
        press(main.child_window(title="Edit", control_type="Button"))
        dialog = edit_dialog(main)
        check(dialog.exists() and dialog.window_text() == "Edit on MyAnimeList" and
              any(text.startswith("One Piece, TV, 1999, on your Watching list") for text in texts(dialog)),
              f"Edit opens a dialog naming the anime ({dialog.window_text() if dialog.exists() else None})")
        if not dialog.exists():
            return
        watched = dialog.child_window(title="Episodes watched", control_type="Edit")
        check(dialog.child_window(title="Status", control_type="ComboBox").selected_text() == "Watching" and
              watched.exists() and watched.get_value() == "1100/?" and
              dialog.child_window(title="Score", control_type="ComboBox").selected_text() == "No score" and
              dialog.child_window(title="Remove from my list", control_type="Button").exists(),
              f"the dialog shows the entry's status, progress as watched/total and score, and offers to remove it "
              f"({watched.get_value() if watched.exists() else None})")
        groups = [group.window_text() for group in dialog.descendants(control_type="Group")]
        start = dialog.child_window(title="Start date", control_type="Group")
        year, month, day = (start.child_window(title=name, control_type="Edit") for name in ("Year", "Month", "Day"))
        check(groups == ["Start date", "Finish date"] and year.exists() and month.exists() and day.exists() and
              (year.get_value(), month.get_value(), day.get_value()) == ("", "", ""),
              f"the dates are two groups of year, month and day, empty when unset ({groups})")
        dialog.child_window(title="Score", control_type="ComboBox").select("10, Masterpiece")
        watched.set_focus()
        wait_for(watched.has_keyboard_focus, 5)
        send_keys("^a", vk_packet=False)
        send_keys("x1101y-")
        check(watched.get_value() == "1101", f"typing in the episode field keeps digits only ({watched.get_value()})")
        send_keys("{UP}")
        check(wait_for(lambda: watched.get_value() == "1102/?", 5) is not None,
              f"Up steps the count and shows it as watched/total ({watched.get_value()})")
        send_keys("3")
        check(watched.get_value() == "11023/?", f"typing after a step carries on the number ({watched.get_value()})")
        send_keys("{BACKSPACE}{DOWN}")
        check(wait_for(lambda: watched.get_value() == "1101/?", 5) is not None,
              f"Down steps back ({watched.get_value()})")
        send_keys("^a", vk_packet=False)
        send_keys("{BACKSPACE}{TAB}")
        check(wait_for(lambda: watched.get_value() == "1100/?", 5) is not None,
              f"a field left empty goes back to the count the entry has ({watched.get_value()})")
        watched.set_focus()
        wait_for(watched.has_keyboard_focus, 5)
        send_keys("^a", vk_packet=False)
        send_keys("1101{TAB}")
        check(wait_for(lambda: watched.get_value() == "1101/?", 5) is not None,
              f"leaving the field shows the count as watched/total ({watched.get_value()})")
        check(not dialog.descendants(control_type="ComboBox")[2:] and not dialog.descendants(control_type="Spinner"),
              "episodes and dates are plain edit fields, one control each")
        year.set_focus()
        wait_for(year.has_keyboard_focus, 5)
        send_keys("{UP}")
        check(year.get_value() == str(time.localtime().tm_year), f"Up in an empty year field starts at this year ({year.get_value()})")
        send_keys("{UP}")
        check(year.get_value() == str(time.localtime().tm_year), f"Up stops at this year ({year.get_value()})")
        send_keys("{DOWN}")
        check(year.get_value() == str(time.localtime().tm_year - 1), f"Down steps the year back ({year.get_value()})")
        send_keys("^a", vk_packet=False)
        send_keys("20q26")
        check(year.get_value() == "2026", f"typing in the year field keeps digits only ({year.get_value()})")
        month.set_edit_text("13")
        day.set_edit_text("1")
        press(dialog.child_window(title="Save", control_type="Button"))
        warning = dialog.child_window(title="MyAnimeList", control_type="Window")
        check(wait_for(warning.exists, 10) is not None and not writes_to(21),
              "a date that isn't real is refused before anything is sent")
        if warning.exists():
            warning.child_window(title="OK", control_type="Button").invoke()
        check(wait_for(month.has_keyboard_focus, 5) is not None, "focus goes to the part of the date that's wrong")
        year.set_edit_text(str(time.localtime().tm_year + 1))
        month.set_edit_text("1")
        press(dialog.child_window(title="Save", control_type="Button"))
        check(wait_for(warning.exists, 10) is not None and
              any("later than this one" in text for text in texts(warning)) and not writes_to(21),
              "a year after this one is refused before anything is sent")
        if warning.exists():
            warning.child_window(title="OK", control_type="Button").invoke()
        check(wait_for(year.has_keyboard_focus, 5) is not None, "focus goes to the year that's too late")
        year.set_edit_text("2026")
        month.set_edit_text("10")
        dialog.child_window(title="Save", control_type="Button").invoke()
        expected = {"score": "10", "num_watched_episodes": "1101", "start_date": "2026-10-01"}
        check(wait_for(lambda: writes_to(21) == [("PATCH", expected)], 10) is not None,
              f"Save sends only the fields that changed ({writes_to(21)})")
        check(wait_for(lambda: "One Piece, 1101 episodes watched, score 10" in labels(anime), 10) is not None,
              f"the list shows the saved values ({labels(anime)})")
        time.sleep(1)
        check(not any("Saved" in line for line in spoken(speech_log)), "the save isn't announced")

        items(anime)[1].select()
        press(main.child_window(title="Remove", control_type="Button"))
        confirm = main.child_window(title="MyAnimeList", control_type="Window")
        check(wait_for(confirm.exists, 10) is not None and
              any("Remove One Piece from your Watching list?" in text for text in texts(confirm)),
              "Remove asks first")
        if confirm.exists():
            confirm.child_window(title_re="&?Yes", control_type="Button").invoke()
        check(wait_for(lambda: ("DELETE", {}) in writes_to(21) and labels(anime) == wanted[:1], 10) is not None,
              f"confirming removes it from MyAnimeList and from the list ({labels(anime)})")
        time.sleep(1)
        check(not any("Removed" in line for line in spoken(speech_log)), "the removal isn't announced")

        anime.set_focus()
        items(anime)[0].select()
        send_keys("{ENTER}")
        search = main.child_window(title="Search", control_type="Edit")
        check(wait_for(search.exists, 10) is not None and search.get_value() == "Frieren: Beyond Journey's End",
              "Enter on an anime goes back to browsing and searches for it")
        results = main.child_window(title="Results", control_type="List")
        episodes = main.child_window(title="Episodes", control_type="List")
        episode_items = wait_for(lambda: items(episodes) if len(items(episodes)) > 5 else None, 45)
        check(episode_items is not None and len(episode_items) == 28,
              f"it finds the show with the current provider and lists its episodes ({len(episode_items or [])})")
        if not episode_items:
            return
        selected = [item.window_text() for item in items(results) if item.is_selected()]
        check(len(selected) == 1 and selected[0].startswith("Frieren: Beyond Journey's End, TV"),
              f"the matching result is the one selected ({selected})")
        check(wait_for(lambda: episode_items[5].is_selected() and
                       (episodes.has_keyboard_focus() or episode_items[5].has_keyboard_focus()), 10) is not None,
              "focus lands on the next episode to watch, the sixth")

        before_playing = len(spoken(speech_log))
        send_keys("{ENTER}")
        playing = wait_for(lambda: re.match(r"Episode 6: .* - Ryu$", main.window_text()), 60)
        check(playing is not None, f"the episode plays ({main.window_text()})")
        check(wait_for(lambda: ("PATCH", {"num_watched_episodes": "6"}) in writes_to(52991), 60) is not None,
              f"once it is mostly played, MyAnimeList gets the new count and nothing else ({writes_to(52991)})")
        status = main.child_window(control_type="StatusBar")
        check(wait_for(lambda: "Episode 6 marked as watched on MyAnimeList" in
                       " ".join(text.window_text() for text in status.descendants()), 10) is not None,
              "the status bar says the episode was marked")
        time.sleep(3)
        check(len(writes_to(52991)) == 1, f"the episode is only counted once ({writes_to(52991)})")
        said = [line for line in spoken(speech_log)[before_playing:] if "MyAnimeList" in line]
        check(not said, f"nothing is spoken when progress is tracked ({said})")
        send_keys("{ESC}")
        wait_for(search.exists, 10)

        results.set_focus()
        send_keys("^m", vk_packet=False)
        dialog = edit_dialog(main)
        check(dialog.exists() and dialog.window_text() == "Edit on MyAnimeList" and
              dialog.child_window(title="Episodes watched", control_type="Edit").get_value() == "6/28" and
              not dialog.child_window(title="Anime", control_type="ComboBox").exists(),
              "Ctrl+M on a result that's on a list opens it for editing, with the new count")
        if dialog.exists():
            dialog.child_window(title="Cancel", control_type="Button").invoke()
            wait_for(lambda: not dialog.exists(), 5)

        search.set_edit_text("one piece")
        main.child_window(title="Search", control_type="Button").invoke()
        found = wait_for(lambda: [item for item in items(results) if item.window_text().startswith("One Piece, TV")], 30)
        check(bool(found), f"searching for One Piece finds the series ({labels(results)[:3]})")
        if not found:
            return
        results.set_focus()
        found[0].select()
        send_keys("^m", vk_packet=False)
        dialog = edit_dialog(main)
        chosen = dialog.child_window(title="Anime", control_type="ComboBox")
        check(dialog.exists() and dialog.window_text() == "Add to MyAnimeList" and chosen.exists() and
              chosen.selected_text() == "One Piece, TV, 1999" and
              not dialog.child_window(title="Remove from my list", control_type="Button").exists(),
              f"Ctrl+M on a result that isn't listed looks it up and offers to add it "
              f"({chosen.selected_text() if chosen.exists() else None})")
        if dialog.exists():
            dialog.child_window(title="Status", control_type="ComboBox").select("Plan to watch")
            dialog.child_window(title="Save", control_type="Button").invoke()
        check(wait_for(lambda: ("PATCH", {"status": "plan_to_watch"}) in writes_to(21), 10) is not None,
              f"saving adds it to the chosen list ({writes_to(21)})")

        search.set_edit_text("hunter x hunter")
        main.child_window(title="Search", control_type="Button").invoke()
        for length, wanted_title, wanted_choice in (
                ("148", "Add to MyAnimeList", "Hunter x Hunter, TV, 2011, 148 episodes"),
                ("62", "Edit on MyAnimeList", "Hunter x Hunter, TV, 1999, 62 episodes, on your Completed list")):
            found = wait_for(lambda: [item for item in items(results)
                                      if item.window_text().startswith(f"Hunter x Hunter, TV, {length} subbed")], 30)
            if not found:
                check(False, f"searching finds the {length}-episode Hunter x Hunter ({labels(results)[:4]})")
                continue
            results.set_focus()
            found[0].select()
            send_keys("^m", vk_packet=False)
            dialog = edit_dialog(main)
            chosen = dialog.child_window(title="Anime", control_type="ComboBox")
            check(dialog.exists() and dialog.window_text() == wanted_title and chosen.exists() and
                  chosen.selected_text() == wanted_choice,
                  f"Ctrl+M on the {length}-episode Hunter x Hunter picks the series of that length, not its namesake "
                  f"({dialog.window_text() if dialog.exists() else None}: "
                  f"{chosen.selected_text() if chosen.exists() else None})")
            if dialog.exists():
                dialog.child_window(title="Cancel", control_type="Button").invoke()
                wait_for(lambda: not dialog.exists(), 5)
        check(not writes_to(136) and not writes_to(11061), "and nothing is changed by looking")

        chooser, anime = open_lists(main)
        chooser.select("Plan to watch")
        check(wait_for(lambda: "One Piece" in labels(anime), 10) is not None,
              f"the added anime shows up on that list ({labels(anime)})")
        send_keys("{ESC}")
        check(wait_for(lambda: results.exists() and
                       (results.has_keyboard_focus() or any(i.has_keyboard_focus() for i in items(results))), 10)
              is not None, "Escape leaves the lists and puts focus back where it was")
        main.close()
        process.wait(15)
    finally:
        if process.poll() is None:
            process.kill()


def main(exe):
    root = tempfile.mkdtemp(prefix="ryu-mal-ui-")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeMal)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for name, flow in (("login", login_flow), ("lists", lists_flow)):
            folder = os.path.join(root, name)
            os.makedirs(folder)
            settings = os.path.join(folder, "settings.ini")
            speech_log = os.path.join(folder, "speech.log")
            urls_file = os.path.join(folder, "opened.txt")
            env = dict(os.environ, RYU_CONFIG_FILE=settings, RYU_MPV_OPTIONS="ao=null,start=88%",
                       RYU_LOG=os.path.join(folder, "ryu.log"), RYU_SPEECH_LOG=speech_log,
                       RYU_UPDATE_FEED="http://127.0.0.1:9/none",
                       RYU_MAL_SERVER=f"http://127.0.0.1:{server.server_address[1]}", RYU_OPENED_URLS=urls_file)
            try:
                if flow is login_flow:
                    flow(exe, env, settings, speech_log, urls_file)
                else:
                    flow(exe, env, speech_log, urls_file)
            except Exception as error:
                check(False, f"unexpected error in the {name} test: {error!r}")
    finally:
        server.shutdown()
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
