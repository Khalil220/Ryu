import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

from pywinauto import Application
from pywinauto.keyboard import send_keys

from test_ui import check, items, wait_for
import test_ui


def launch(exe, env):
    process = subprocess.Popen([exe], env=env)
    app = Application(backend="uia").connect(process=process.pid, timeout=20)
    app.window(title_re="Ryu.*").wait("visible", timeout=20)
    return process, app.window(handle=app.window(title_re="Ryu.*").handle)


def menu_items(main):
    return [item for item in main.descendants(control_type="MenuItem")
            if item.window_text() not in ("System", "File", "Help")]


def file_menu(main):
    main.set_focus()
    send_keys("%f", vk_packet=False)
    wait_for(lambda: menu_items(main) or None, 5)
    return menu_items(main)


def recent_menu(main):
    opened = file_menu(main)
    recent = [item for item in opened if item.window_text() == "Recently watched"]
    if not recent or not recent[0].is_enabled():
        close_menus()
        return None
    send_keys("r", vk_packet=False)
    entries = wait_for(lambda: [item.window_text() for item in menu_items(main)
                                if re.match(r"\d+ ", item.window_text()) or item.window_text().startswith("Clear")]
                       or None, 5)
    return entries or []


def close_menus():
    send_keys("{ESC}{ESC}{ESC}")
    time.sleep(0.3)


def play(main, query, show_start, row):
    search = main.child_window(title="Search", control_type="Edit")
    search.set_edit_text(query)
    main.child_window(title="Search", control_type="Button").invoke()
    results = main.child_window(title="Results", control_type="List")
    found = wait_for(lambda: [item for item in items(results) if item.window_text().startswith(show_start)], 30)
    if not found:
        return False
    results.set_focus()
    found[0].select()
    send_keys("{ENTER}")
    episodes = main.child_window(title="Episodes", control_type="List")
    rows = wait_for(lambda: items(episodes) if len(items(episodes)) > row else None, 45)
    if not rows:
        return False
    episodes.set_focus()
    rows[row].select()
    send_keys("{ENTER}")
    return wait_for(lambda: re.match(rf"Episode {row + 1}\b.* - Ryu$", main.window_text()), 60) is not None


def leave_player(main):
    wait_for(main.child_window(title="Pause", control_type="Button").has_keyboard_focus, 15)
    send_keys("{ESC}")
    wait_for(main.child_window(title="Search", control_type="Edit").exists, 10)


def run(exe, root):
    settings = os.path.join(root, "settings.ini")
    env = dict(os.environ, RYU_CONFIG_FILE=settings, RYU_MPV_OPTIONS="ao=null", RYU_LOG=os.path.join(root, "ryu.log"),
               RYU_SPEECH_LOG=os.path.join(root, "speech.log"), RYU_UPDATE_FEED="http://127.0.0.1:9/none")
    process, main = launch(exe, env)
    try:
        opened = file_menu(main)
        recent = [item for item in opened if item.window_text() == "Recently watched"]
        check(len(recent) == 1 and not recent[0].is_enabled(),
              f"File has a Recently watched menu, unavailable while nothing has been watched "
              f"({[item.window_text() for item in opened]})")
        close_menus()

        check(play(main, "frieren", "Frieren: Beyond Journey's End, TV", 1), "Frieren episode 2 plays")
        leave_player(main)
        episodes = main.child_window(title="Episodes", control_type="List")
        episodes.set_focus()
        items(episodes)[2].select()
        send_keys("{ENTER}")
        check(wait_for(lambda: re.match(r"Episode 3\b.* - Ryu$", main.window_text()), 60) is not None,
              "Frieren episode 3 plays")
        leave_player(main)
        check(play(main, "teasing master takagi-san", "Teasing Master Takagi-san, TV", 0),
              "Teasing Master Takagi-san episode 1 plays")
        leave_player(main)

        wanted = ["1 Teasing Master Takagi-san episode 1", "2 Frieren: Beyond Journey's End episode 3",
                  "Clear recently watched"]
        entries = recent_menu(main)
        check(entries == wanted, f"the menu has one entry per show, newest first, at its latest episode ({entries})")
        close_menus()
        main.close()
        process.wait(15)

        process, main = launch(exe, env)
        entries = recent_menu(main)
        check(entries == wanted, f"the entries are still there after restarting Ryu ({entries})")
        send_keys("2", vk_packet=False)
        check(wait_for(lambda: re.match(r"Episode 3\b.* - Frieren: Beyond Journey's End - Ryu$", main.window_text()), 90)
              is not None, f"choosing an entry plays its episode straight away ({main.window_text()})")
        leave_player(main)
        search = main.child_window(title="Search", control_type="Edit")
        results = main.child_window(title="Results", control_type="List")
        episodes = main.child_window(title="Episodes", control_type="List")
        selected = [item.window_text() for item in items(episodes) if item.is_selected()]
        check(search.get_value() == "Frieren: Beyond Journey's End" and len(items(results)) == 1 and
              len(selected) == 1 and selected[0].startswith("Episode 3"),
              f"leaving the player lands on that show's episode list, on the episode ({selected})")
        entries = recent_menu(main)
        check(entries == ["1 Frieren: Beyond Journey's End episode 3", "2 Teasing Master Takagi-san episode 1",
                          "Clear recently watched"], f"the resumed show moves to the top ({entries})")
        close_menus()

        main.set_focus()
        send_keys("^p", vk_packet=False)
        preferences = main.child_window(title="Preferences", control_type="Window")
        if wait_for(preferences.exists, 10) is not None:
            preferences.child_window(title="Provider", control_type="ComboBox").select("Miruro")
            preferences.child_window(title="OK", control_type="Button").invoke()
        wait_for(lambda: main.window_text() == "Ryu - Miruro", 10)
        entries = recent_menu(main)
        send_keys("1", vk_packet=False)
        check(wait_for(lambda: re.match(r"Episode 3\b.* - Frieren: Beyond Journey's End - Ryu$", main.window_text()), 90)
              is not None, f"an entry from another provider is found on the current one and played ({main.window_text()})")
        leave_player(main)

        entries = recent_menu(main)
        check(bool(entries) and entries[-1] == "Clear recently watched" and len(entries) == 3,
              f"watching it on the other provider updates its entry instead of adding one ({entries})")
        send_keys("c", vk_packet=False)
        time.sleep(0.5)
        check(recent_menu(main) is None, "Clear recently watched empties the menu and makes it unavailable")

        def saved():
            with open(settings, encoding="utf-8") as f:
                return f.read()

        check(wait_for(lambda: "[Recent" not in saved(), 10) is not None, "and removes the entries from the settings file")
        main.close()
        process.wait(15)
    finally:
        if process.poll() is None:
            process.kill()


def main(exe):
    root = tempfile.mkdtemp(prefix="ryu-recent-ui-")
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
