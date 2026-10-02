# Ryu

An accessible anime player for Windows, written in C++ with wxWidgets and libmpv.

## Features

- Streams from HiAnime, AniZone and Miruro, falling back to the other sites when an episode won't play
- Subbed and dubbed audio, with the episode list limited to what's available in each
- Every subtitle track an episode offers, and optional reading of subtitle lines through the screen reader
- Audio language choice on episodes with several dubs
- Skip intro on episodes that have intro times
- Swaps banned video hosts for working ones before playback
- MyAnimeList list browsing and editing, with episodes tracked as they're watched

## Keyboard shortcuts

- Space: play or pause
- Left and Right: seek 10 seconds, or 60 with Shift
- Up and Down: volume
- T: current time
- I: skip intro
- R: toggle subtitle reading
- N and P: next and previous episode
- F11: full screen
- Escape: leave full screen, or go back to the episode list
- Ctrl+D in the results: speak the selected show's synopsis
- Ctrl+M in the results: add the show to MyAnimeList, or edit or remove it
- Ctrl+L: your MyAnimeList lists
- Ctrl+P: preferences
- F1: all keyboard shortcuts

## Building

Requirements:

- Visual Studio 2026 with the Desktop development with C++ workload
- CMake 4.2 or newer, the first release with the Visual Studio 2026 generator
- A separate clone of vcpkg. The copy bundled with Visual Studio 2026 (tool version 2025-09-03) fails with "Unable to find a valid Visual Studio instance".

```
git clone https://github.com/microsoft/vcpkg
vcpkg\bootstrap-vcpkg.bat -disableMetrics
setx VCPKG_ROOT "%CD%\vcpkg"
```

Then, from a new terminal in the Ryu folder:

```
cmake --preset windows
cmake --build --preset release
```

Visual Studio's developer prompts point `VCPKG_ROOT` at the bundled copy. Building from one of them, with the CMake that ships with Visual Studio, works once `VCPKG_ROOT` is set back to the clone.

The first configure builds wxWidgets, libcurl, lexbor and nlohmann-json through vcpkg, and downloads prebuilt libmpv and Prism. The result is `build\windows\src\app\Release\ryu.exe`, which needs `libmpv-2.dll` and `prism.dll` beside it.

`tools\release.ps1` builds a release from a clean checkout of the current commit, runs the unit tests, and writes `dist\Ryu-<version>-win64.zip` with a `SHA256SUMS` file. With `-Publish -NotesFile <file>` it also tags the version from `CMakeLists.txt` and uploads both files as a GitHub release with those notes, which is what the built-in updater reads and shows.

## Tests

The test presets use the debug build (`cmake --build --preset debug`).

- `ctest --preset unit`: offline, against saved site responses
- `ctest --preset live`: resolves real episodes from all three sites and queries MyAnimeList
- `ctest --preset ui`: drives the app through UI Automation, including a full update and a MyAnimeList session against local test servers, and takes over the keyboard for about four minutes

The UI tests need Python with pywinauto:

```
python -m venv build\ui-venv
build\ui-venv\Scripts\python -m pip install -r tests\ui\requirements.txt
cmake --preset windows -DRYU_UI_TEST_PYTHON=%CD%\build\ui-venv\Scripts\python.exe
```

## Files

Settings are stored in `%APPDATA%\Ryu\settings.ini`. Ryu can only update itself from a folder you can write to. From a protected folder such as Program Files, it links to the release page instead. Each session is logged to `%APPDATA%\Ryu\ryu.log`, with the previous session kept in `ryu.old.log`.

These environment variables override the defaults:

- `RYU_CONFIG_FILE`: settings file
- `RYU_LOG`: log file
- `RYU_SPEECH_LOG`: file that receives every spoken announcement
- `RYU_MPV_OPTIONS`: extra mpv options as comma-separated `name=value` pairs
- `RYU_UPDATE_FEED`: address of the release feed the updater reads
- `RYU_MAL_SERVER`: server to use in place of MyAnimeList's login and API hosts
- `RYU_OPENED_URLS`: file that receives addresses Ryu would have opened in the browser

## Command-line tool

`ryu-cli` queries the sites without the GUI: `providers`, `search <query>`, `episodes <show id>` and `streams <episode id> [sub|dub]`, each taking `--provider <id>` and `--base-url <url>`.

## Contributing

Ryu is a personal project I maintain in my free time, if at all. I don't intend to accept pull requests.

## License

Ryu is licensed under the GNU General Public License v3.0. The prebuilt libmpv it downloads is also GPL-licensed, and Prism is licensed under the MPL 2.0.
