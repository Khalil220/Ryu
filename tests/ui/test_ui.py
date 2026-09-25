import os
import re
import subprocess
import sys
import tempfile
import time

from pywinauto import Application
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


def items(list_box):
    return list_box.children(control_type="ListItem")


def run(app):
    main = app.window(title_re="Ryu.*")
    main.wait("visible", timeout=20)
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

    main.child_window(title="Play", control_type="Button").invoke()
    player = app.window(title_re="Episode 1.*")
    check(wait_for(player.exists, 45) is not None, "Play opens the player window")
    if not player.exists():
        return
    check(re.match(r"Episode 1: The Journey's End - Frieren.* - Ryu$", player.window_text()) is not None,
          f"player title names the episode ({player.window_text()})")

    time_box = player.child_window(title="Time", control_type="Edit")
    check(wait_for(lambda: seconds(time_box.get_value()) >= 3, 60) is not None,
          f"playback advances the clock ({time_box.get_value()})")
    check(re.search(r" of \d+:\d\d", time_box.get_value()) is not None,
          f"time shows the duration ({time_box.get_value()})")

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

    check(player.child_window(title="Close", control_type="Button", class_name="Button").exists(),
          "player has a Close button")
    send_keys("{ESC}")
    check(wait_for(lambda: not player.exists(), 15) is not None, "Escape closes the player")

    main.set_focus()
    search.set_focus()
    send_keys("^,")
    preferences = main.child_window(title="Preferences", control_type="Window")
    check(wait_for(preferences.exists, 10) is not None, "Ctrl+comma opens Preferences")
    if preferences.exists():
        base_url = preferences.child_window(title="Base URL", control_type="Edit")
        check(base_url.get_value() == "https://hianime.at", f"base URL shows the default ({base_url.get_value()})")
        check(preferences.child_window(title="Provider", control_type="ComboBox").exists(),
              "provider choice is labelled")
        preferences.child_window(title="Cancel", control_type="Button").invoke()
        check(wait_for(lambda: not preferences.exists(), 10) is not None, "Cancel closes Preferences")

    main.close()


def main(exe):
    config = os.path.join(tempfile.gettempdir(), f"ryu-ui-test-{os.getpid()}.ini")
    env = dict(os.environ, RYU_CONFIG_FILE=config, RYU_MPV_OPTIONS="ao=null")
    process = subprocess.Popen([os.path.abspath(exe)], env=env)
    try:
        run(Application(backend="uia").connect(process=process.pid, timeout=20))
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
        if os.path.exists(config):
            os.remove(config)
    print(f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
