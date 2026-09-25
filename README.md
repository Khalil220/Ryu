# Ryu

Ryu is an anime client for Windows built for screen reader users. It is written in C++ with wxWidgets and plays video through libmpv. Every control is a standard Windows control with an explicit accessible name, so NVDA, JAWS and Narrator read it without guesswork.

Ryu currently supports one provider, HiAnime (hianime.at). It can search, list episodes, play subbed or dubbed episodes, and load English subtitles.

## Building

You need Visual Studio 2026 with the "Desktop development with C++" workload, CMake 4.2 or newer, and a current vcpkg checkout. The vcpkg that ships inside Visual Studio 2026 is too old to recognize the v145 compiler toolset, so use a separate clone:

1. `git clone https://github.com/microsoft/vcpkg C:\Users\<you>\vcpkg`
2. `C:\Users\<you>\vcpkg\bootstrap-vcpkg.bat -disableMetrics`
3. `setx VCPKG_ROOT C:\Users\<you>\vcpkg`, then open a new terminal.
4. `cmake --preset windows`
5. `cmake --build --preset debug`

The first configure builds wxWidgets, libcurl, lexbor and nlohmann-json through vcpkg, which takes a while. It also downloads a prebuilt libmpv from shinchiro's mpv-winbuild-cmake releases and generates an MSVC import library for it. The app ends up at `build\windows\src\app\Debug\ryu.exe` with `libmpv-2.dll` next to it.

## Using Ryu

The main window has a search box, a results list, an episode list, an audio choice and a Play button.

1. Type a title in the search box and press Enter. Focus moves to the results list when the search finishes.
2. Press Enter on a result to load its episodes. Focus moves to the episode list.
3. Press Enter on an episode, or use the Play button, to start playing it in the player window.

Alt+S, Alt+R, Alt+E, Alt+A and Alt+P jump to the search box, results, episodes, audio choice and Play button. Ctrl+comma opens Preferences.

In the player window:

1. Space pauses and resumes. When focus is on a button, Space presses that button instead.
2. Left and Right arrows seek 10 seconds. Shift with Left or Right seeks 60 seconds.
3. Up and Down arrows change the volume by 5.
4. Escape closes the player.
5. Alt+P is Pause or Play, Alt+B goes back 10 seconds, Alt+F goes forward 10 seconds and Alt+C closes the player.
6. Alt+T moves to the position slider, Alt+M to the time field and Alt+V to the volume slider. Arrow keys move the sliders when they have focus, and move the cursor in the time field.

The time field reads like "3:21 of 24:10" and updates once a second.

## Preferences

Preferences holds the provider, the provider's base URL and the preferred audio. The base URL is there for when a site moves to a new domain or you want a mirror. Reset to default puts back the built-in address. Settings are stored in `%APPDATA%\Ryu\settings.ini`.

## Command-line tool

`ryu-cli` drives the providers without the GUI and prints one plain line per item:

1. `ryu-cli providers`
2. `ryu-cli search "frieren"`
3. `ryu-cli episodes 481`
4. `ryu-cli streams 9227 dub`

Every command accepts `--provider <id>` and `--base-url <url>`.

## Tests

There are three suites.

1. `ctest --preset unit` runs offline tests against saved responses in `tests/fixtures`.
2. `ctest --preset live` resolves a real episode from hianime.at and checks the playlist and subtitles download.
3. `ctest --preset ui` launches Ryu and drives it through Windows UI Automation. It searches, opens episodes, plays one, pauses, seeks, closes the player and opens Preferences. It takes over focus and the keyboard for about a minute, and mpv's audio is muted while it runs.

The UI test needs Python with pywinauto. Set it up once:

1. `python -m venv build\ui-venv`
2. `build\ui-venv\Scripts\python -m pip install -r tests\ui\requirements.txt`
3. `cmake --preset windows -DRYU_UI_TEST_PYTHON=%CD%\build\ui-venv\Scripts\python.exe`

## Environment variables

`RYU_CONFIG_FILE` points Ryu at a different settings file. `RYU_MPV_OPTIONS` passes extra mpv options as comma-separated `name=value` pairs, for example `ao=null` to mute playback.

## Licensing note

mpv is licensed under the GPL unless it is built with `-Dgpl=false`, and the prebuilt libmpv Ryu downloads is a default build. Plan on GPL terms if you distribute Ryu with it.
