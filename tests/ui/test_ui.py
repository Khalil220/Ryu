import os
import re
import subprocess
import sys
import tempfile
import time

from pywinauto import Application, Desktop
from pywinauto.keyboard import send_keys

failures = 0


def check(condition, message):
    global failures
    print(("PASS: " if condition else "FAIL: ") + message, flush=True)
    if not condition:
        failures += 1


def wait_for(probe, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            result = probe()
        except Exception:
            result = None
        if result:
            return result
        time.sleep(0.25)
    return None


def seconds(clock):
    match = re.match(r"^(?:(\d+):)?(\d+):(\d\d)", clock or "")
    if not match:
        return -1
    hours, minutes, secs = match.groups()
    return int(hours or 0) * 3600 + int(minutes) * 60 + int(secs)


def speech_viewer():
    for window in Desktop(backend="win32").windows():
        if window.window_text() == "NVDA Speech Viewer":
            for child in window.descendants():
                if child.class_name().startswith("RICHEDIT"):
                    return child
    return None


def heard_since(viewer, mark):
    return [line for line in viewer.window_text()[mark:].replace("\r", "\n").split("\n") if line.strip()]


def items(list_box):
    return list_box.children(control_type="ListItem")


def spoken(log_path):
    try:
        with open(log_path, encoding="utf-8") as log:
            return [line.rstrip("\n") for line in log]
    except FileNotFoundError:
        return []


def run(app, speech_log):
    app.window(title_re="Ryu.*").wait("visible", timeout=20)
    main = app.window(handle=app.window(title_re="Ryu.*").handle)
    check(main.window_text() == "Ryu - HiAnime", f"main window title names the provider ({main.window_text()})")

    search = main.child_window(title="Search", control_type="Edit")
    check(search.exists(), "search box is labelled Search")
    search.set_edit_text("frieren")
    main.child_window(title="Search", control_type="Button").invoke()

    results = main.child_window(title="Results", control_type="List")
    first = wait_for(lambda: items(results)[0])
    check(first is not None, "search fills the results list")
    if first is None:
        return
    check(re.match(r"Frieren: Beyond Journey.s End, TV, \d+ subbed", first.window_text()) is not None,
          f"first result reads well ({first.window_text()})")
    check(wait_for(lambda: results.has_keyboard_focus() or first.has_keyboard_focus(), 5) is not None,
          "focus moves to the results list")

    viewer = speech_viewer()
    if viewer is None:
        print("SKIP: NVDA Speech Viewer is not open, so what NVDA says on entering the list is not checked", flush=True)
    else:
        search.set_focus()
        time.sleep(1.5)
        send_keys("{TAB}")
        time.sleep(1.5)
        mark = len(viewer.window_text())
        send_keys("{TAB}")
        time.sleep(2)
        heard = heard_since(viewer, mark)
        check(any(first.window_text() in line and re.search(r"\b1 of 4\b", line) for line in heard),
              f"NVDA says the first result with its position when tabbing into the list ({heard})")

    results.set_focus()
    first.select()
    send_keys("{ENTER}")
    episodes = main.child_window(title="Episodes", control_type="List")
    episode_items = wait_for(lambda: items(episodes) or None)
    check(episode_items is not None and len(episode_items) == 28,
          f"Enter on a result lists its episodes ({len(episode_items or [])})")
    if not episode_items:
        return
    check(episode_items[0].window_text() == "Episode 1: The Journey's End",
          f"episode label reads well ({episode_items[0].window_text()})")
    check(wait_for(lambda: episodes.has_keyboard_focus() or episode_items[0].has_keyboard_focus(), 5) is not None,
          "focus moves to the episode list")

    audio = main.child_window(title="Audio", control_type="ComboBox")
    audio.select("Dubbed")
    check(audio.selected_text() == "Dubbed", f"audio choice switches to Dubbed ({audio.selected_text()})")

    main.child_window(title="Play", control_type="Button").invoke()
    player = main
    playing_one = wait_for(lambda: re.match(r"Episode 1: The Journey's End - Frieren.* - Ryu$", main.window_text()), 45)
    check(playing_one is not None, f"Play switches the window to the player and names the episode ({main.window_text()})")
    if playing_one is None:
        return
    check(len(app.windows()) == 1, f"playing keeps Ryu to one window ({len(app.windows())})")
    check(wait_for(lambda: main.child_window(title="Pause", control_type="Button").has_keyboard_focus(), 10)
          is not None, "focus lands on Pause")

    time_box = player.child_window(title="Time", control_type="Edit")
    check(wait_for(lambda: seconds(time_box.get_value()) >= 3, 60) is not None,
          f"playback advances the clock ({time_box.get_value()})")
    check(re.search(r" of \d+:\d\d", time_box.get_value()) is not None,
          f"time shows the duration ({time_box.get_value()})")

    subtitles = player.child_window(title="Subtitles", control_type="ComboBox")
    check(wait_for(lambda: subtitles.selected_text() == "English", 20) is not None,
          f"English subtitles load and are selected ({subtitles.selected_text()})")

    check(player.child_window(title="Skip intro", control_type="Button").exists(),
          "Skip intro is offered for an episode with intro data")
    check(wait_for(lambda: "Intro" in spoken(speech_log), 10) is not None, "the intro is announced")
    player.child_window(title="Pause", control_type="Button").set_focus()
    before_skip = seconds(time_box.get_value())
    send_keys("i", vk_packet=False)
    check(wait_for(lambda: seconds(time_box.get_value()) >= 88, 20) is not None,
          f"I skips the intro ({before_skip} to {time_box.get_value()})")
    check("Skipped intro" in spoken(speech_log), "skipping the intro is announced")

    player.child_window(title="Pause", control_type="Button").invoke()
    play_button = player.child_window(title="Play", control_type="Button")
    check(wait_for(play_button.exists, 10) is not None, "Pause button turns into Play")
    time.sleep(0.5)
    paused_at = seconds(time_box.get_value())
    time.sleep(3)
    check(seconds(time_box.get_value()) == paused_at, f"clock stops while paused ({paused_at})")

    play_button.invoke()
    check(wait_for(lambda: seconds(time_box.get_value()) >= paused_at + 2, 20) is not None,
          f"clock resumes after Play ({time_box.get_value()})")

    before_seek = seconds(time_box.get_value())
    player.child_window(title="Forward 10 seconds", control_type="Button").invoke()
    check(wait_for(lambda: seconds(time_box.get_value()) >= before_seek + 10, 15) is not None,
          f"Forward 10 seconds seeks ahead ({before_seek} to {time_box.get_value()})")

    pause_button = player.child_window(title="Pause", control_type="Button")
    pause_button.set_focus()
    before_key = seconds(time_box.get_value())
    send_keys("{RIGHT}")
    check(wait_for(lambda: seconds(time_box.get_value()) >= before_key + 10, 15) is not None,
          f"Right arrow seeks ahead ({before_key} to {time_box.get_value()})")

    time_box.set_focus()
    send_keys("{SPACE}")
    check(wait_for(play_button.exists, 10) is not None, "Space pauses from the time field")
    send_keys("{SPACE}")
    check(wait_for(pause_button.exists, 10) is not None, "Space resumes")

    pause_button.set_focus()
    send_keys("t", vk_packet=False)
    check(wait_for(lambda: any(re.match(r"^\d+:\d\d of \d+:\d\d$", line) for line in spoken(speech_log)), 5)
          is not None, "T announces the time")
    check(pause_button.has_keyboard_focus(), "T leaves focus where it was")
    send_keys("{DOWN}")
    check(wait_for(lambda: "Volume 95" in spoken(speech_log), 5) is not None, "Down arrow lowers and announces the volume")

    read = player.child_window(title="Read subtitles aloud", control_type="CheckBox")
    check(read.get_toggle_state() == 0, "subtitles are not read aloud by default for a dub")
    pause_button.set_focus()
    mark = len(spoken(speech_log))
    send_keys("r", vk_packet=False)
    check(wait_for(lambda: read.get_toggle_state() == 1, 5) is not None, "R turns subtitle reading on")
    check(wait_for(lambda: "Reading subtitles" in spoken(speech_log)[mark:], 5) is not None,
          "turning subtitle reading on is announced")

    pause_button.set_focus()
    send_keys("p", vk_packet=False)
    check(wait_for(lambda: "This is the first episode" in spoken(speech_log), 5) is not None,
          "P on the first episode says it is the first")
    check(main.window_text().startswith("Episode 1:"), f"P on the first episode stays put ({main.window_text()})")

    send_keys("^p", vk_packet=False)
    player_preferences = main.child_window(title="Preferences", control_type="Window")
    check(wait_for(player_preferences.exists, 10) is not None, "Ctrl+P opens Preferences from the player")
    if player_preferences.exists():
        player_preferences.child_window(title="Cancel", control_type="Button").invoke()
        wait_for(lambda: not player_preferences.exists(), 10)
    check(main.window_text().startswith("Episode 1:"), "Ctrl+P does not step to the previous episode")
    pause_button.set_focus()

    send_keys("n", vk_packet=False)
    check(wait_for(lambda: main.window_text().startswith("Episode 2:"), 45) is not None,
          f"N plays the next episode ({main.window_text()})")
    check(wait_for(lambda: 1 <= seconds(time_box.get_value()) < 60, 60) is not None,
          f"the next episode plays, repairing its host if it is banned ({time_box.get_value()})")
    check(read.get_toggle_state() == 1, "the subtitle reading choice carries over to the next episode")

    check(player.child_window(title="Close", control_type="Button", class_name="Button").exists(),
          "player has a Close button")
    main.child_window(title="Pause", control_type="Button").set_focus()
    send_keys("{ESC}")
    check(wait_for(lambda: main.window_text() == "Ryu - HiAnime", 15) is not None,
          f"Escape goes back to browsing ({main.window_text()})")
    episode_two = items(episodes)[1]
    check(wait_for(lambda: episode_two.has_keyboard_focus() or episodes.has_keyboard_focus(), 5) is not None,
          "focus returns to the episode list")
    check(episode_two.is_selected(), f"the episode that was playing is selected ({episode_two.window_text()})")

    search.set_focus()
    send_keys("{END}tnp", vk_packet=False)
    check(search.get_value() == "frierentnp", f"player keys do not fire while typing a search ({search.get_value()})")
    search.set_edit_text("frieren")

    audio.select("Subbed")
    items(episodes)[0].select()
    main.child_window(title="Play", control_type="Button").invoke()
    check(wait_for(lambda: main.window_text().startswith("Episode 1:"), 45) is not None,
          f"episode 1 subbed plays, repairing its banned host ({main.window_text()})")
    check(read.get_toggle_state() == 1, "subtitles are read aloud by default for a subbed episode")
    mark = len(spoken(speech_log))
    known = re.compile(r"^(\d+:\d\d( of \d+:\d\d)?|Volume \d+|Paused|Playing|Reading subtitles|Not reading subtitles|"
                       r"Skipped intro|Intro|End of episode|Loading .*|From .*|\[backend\].*)$")
    subtitle_line = wait_for(lambda: next((line for line in spoken(speech_log)[mark:] if not known.match(line)), None),
                             60)
    check(subtitle_line is not None, f"a subtitle line is read aloud ({subtitle_line})")
    main.child_window(title="Pause", control_type="Button").set_focus()
    send_keys("{ESC}")
    check(wait_for(lambda: main.window_text() == "Ryu - HiAnime", 15) is not None, "Escape leaves the subbed episode")
    search.set_focus()

    send_keys("^p", vk_packet=False)
    preferences = main.child_window(title="Preferences", control_type="Window")
    check(wait_for(preferences.exists, 10) is not None, "Ctrl+P opens Preferences")
    if preferences.exists():
        base_url = preferences.child_window(title="Base URL", control_type="Edit")
        check(base_url.get_value() == "https://hianime.at", f"base URL shows the default ({base_url.get_value()})")
        check(preferences.child_window(title="Provider", control_type="ComboBox").exists(),
              "provider choice is labelled")
        fallback = preferences.child_window(title="Try other providers when an episode won't play",
                                            control_type="CheckBox")
        check(fallback.exists() and fallback.get_toggle_state() == 1, "provider fallback is offered and on by default")
        preferences.child_window(title="Cancel", control_type="Button").invoke()
        check(wait_for(lambda: not preferences.exists(), 10) is not None, "Cancel closes Preferences")

    search.set_focus()
    send_keys("^p", vk_packet=False)
    check(wait_for(preferences.exists, 10) is not None, "Preferences opens again")
    if preferences.exists():
        provider = preferences.child_window(title="Provider", control_type="ComboBox")
        provider.select("KickAssAnime")
        base_url = preferences.child_window(title="Base URL", control_type="Edit")
        check(wait_for(lambda: base_url.get_value() == "https://kaa.lt", 5) is not None,
              f"choosing KickAssAnime shows its base URL ({base_url.get_value()})")
        preferences.child_window(title="OK", control_type="Button").invoke()
        check(wait_for(lambda: main.window_text() == "Ryu - KickAssAnime", 10) is not None,
              f"the window title follows the provider ({main.window_text()})")
        search.set_edit_text("frieren")
        main.child_window(title="Search", control_type="Button").invoke()
        first_kaa = wait_for(lambda: items(results)[0])
        check(first_kaa is not None and first_kaa.window_text() ==
              "Frieren: Beyond Journey's End, TV, 2023, subbed and dubbed",
              f"KickAssAnime results read well ({first_kaa.window_text() if first_kaa else None})")

    lines = spoken(speech_log)
    for expected in ["Loading episodes", "Loading Episode 1: The Journey's End", "Paused", "Playing",
                     "This is the first episode", "Loading Episode 2: It Didn't Have to Be Magic..."]:
        check(expected in lines, f"announced: {expected}")
    for unexpected in ["Searching for frieren", "4 results", "28 episodes"]:
        check(unexpected not in lines, f"not announced: {unexpected}")
    backend = [line for line in lines if line.startswith("[backend] ")]
    print(f"INFO: speech backend {backend[0][10:] if backend else 'none (no screen reader running)'}")

    main.close()


def main(exe):
    config = os.path.join(tempfile.gettempdir(), f"ryu-ui-test-{os.getpid()}.ini")
    speech_log = os.path.join(tempfile.gettempdir(), f"ryu-ui-speech-{os.getpid()}.log")
    env = dict(os.environ, RYU_CONFIG_FILE=config, RYU_MPV_OPTIONS="ao=null", RYU_SPEECH_LOG=speech_log)
    process = subprocess.Popen([os.path.abspath(exe)], env=env)
    try:
        run(Application(backend="uia").connect(process=process.pid, timeout=20), speech_log)
        try:
            process.wait(15)
            check(True, "closing the main window exits the app")
        except subprocess.TimeoutExpired:
            check(False, "closing the main window exits the app")
    except Exception as error:
        check(False, f"unexpected error: {error!r}")
    finally:
        if process.poll() is None:
            process.kill()
        for path in (config, speech_log):
            if os.path.exists(path):
                os.remove(path)
    print(f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
