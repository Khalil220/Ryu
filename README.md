# Ryu

Ryu is an anime client for Windows built for screen reader users. It is written in C++ with wxWidgets and plays video through libmpv. Every control is a standard Windows control with an explicit accessible name, so NVDA, JAWS and Narrator read it without guesswork.

Ryu currently supports one provider, HiAnime (hianime.at), whose videos are served through megaplay. It can search, list episodes, play subbed or dubbed episodes, and load every subtitle track the episode offers.

Ryu speaks status updates through the screen reader you are running, using the Prism library. It talks to NVDA, JAWS, Narrator and other screen readers directly, with no extra DLLs. It deliberately never falls back to a SAPI or OneCore voice, so it stays quiet when no screen reader is running.

## Building

You need Visual Studio 2026 with the "Desktop development with C++" workload, CMake 4.2 or newer, and a current vcpkg checkout. The vcpkg that ships inside Visual Studio 2026 is too old to recognize the v145 compiler toolset, so use a separate clone:

1. `git clone https://github.com/microsoft/vcpkg C:\Users\<you>\vcpkg`
2. `C:\Users\<you>\vcpkg\bootstrap-vcpkg.bat -disableMetrics`
3. `setx VCPKG_ROOT C:\Users\<you>\vcpkg`, then open a new terminal.
4. `cmake --preset windows`
5. `cmake --build --preset debug`

The first configure builds wxWidgets, libcurl, lexbor and nlohmann-json through vcpkg, which takes a while. It also downloads a prebuilt libmpv from shinchiro's mpv-winbuild-cmake releases and generates an MSVC import library for it, and downloads the prebuilt Prism 0.18.2 package. The app ends up at `build\windows\src\app\Debug\ryu.exe` with `libmpv-2.dll` and `prism.dll` next to it.

## Using Ryu

The main window has a search box, a results list, an episode list, an audio choice and a Play button.

1. Type a title in the search box and press Enter. Ryu says "Searching", then moves focus to the results list and says how many results it found.
2. Press Enter on a result to load its episodes. Focus moves to the episode list and Ryu says how many episodes there are.
3. Press Enter on an episode, or use the Play button, to start playing it in the player window. Ryu says "Playing" once the video starts.

Alt+S, Alt+R, Alt+E, Alt+A and Alt+P jump to the search box, results, episodes, audio choice and Play button. Ctrl+comma opens Preferences.

In the player window:

1. Space pauses and resumes, and Ryu says "Paused" or "Playing". When focus is on a button, Space presses that button instead.
2. Left and Right arrows seek 10 seconds, and Ryu speaks the new position. Shift with Left or Right seeks 60 seconds.
3. Up and Down arrows change the volume by 5, and Ryu speaks the new volume.
4. T speaks the current time, for example "3:21 of 24:10", without moving focus.
5. Escape closes the player.
6. Alt+P is Pause or Play, Alt+B goes back 10 seconds, Alt+F goes forward 10 seconds and Alt+C closes the player.
7. Alt+T moves to the position slider, Alt+M to the time field, Alt+V to the volume slider and Alt+S to the subtitles choice. Arrow keys move the sliders and change the subtitle track when those have focus, and move the cursor in the time field.

The time field reads like "3:21 of 24:10" and updates once a second. The subtitles choice lists every track that loaded, with the site's default selected, plus Off. Ryu says "End of episode" when playback reaches the end.

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
3. `ctest --preset ui` launches Ryu and drives it through Windows UI Automation. It searches, opens episodes, plays one dubbed, checks the subtitles load, pauses, seeks, uses the player's keys, closes the player and opens Preferences. It also reads a log of everything Ryu announced. It takes over focus and the keyboard for about a minute, your screen reader will speak along with it, and mpv's audio is muted while it runs.

The UI test needs Python with pywinauto. Set it up once:

1. `python -m venv build\ui-venv`
2. `build\ui-venv\Scripts\python -m pip install -r tests\ui\requirements.txt`
3. `cmake --preset windows -DRYU_UI_TEST_PYTHON=%CD%\build\ui-venv\Scripts\python.exe`

## Environment variables

`RYU_CONFIG_FILE` points Ryu at a different settings file. `RYU_MPV_OPTIONS` passes extra mpv options as comma-separated `name=value` pairs, for example `ao=null` to mute playback. `RYU_SPEECH_LOG` appends every announcement, and the speech backend Ryu picked, to the given file.

## Licensing note

mpv is licensed under the GPL unless it is built with `-Dgpl=false`, and the prebuilt libmpv Ryu downloads is a default build. Plan on GPL terms if you distribute Ryu with it. Prism is licensed under the MPL 2.0.
