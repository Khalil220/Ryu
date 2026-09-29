# Ryu

Ryu is an anime client for Windows built for screen reader users. It is written in C++ with wxWidgets and plays video through libmpv. Every control is a standard Windows control with an explicit accessible name, so NVDA, JAWS and Narrator read it without guesswork.

Ryu supports two providers. HiAnime (hianime.at) serves its videos through megaplay, and KickAssAnime (kaa.lt) hosts its own. Both can search, list episodes, play subbed or dubbed episodes, and load every subtitle track the episode offers.

These sites store video on throwaway domains that Cloudflare bans from time to time. Before playing an episode, Ryu checks each video host it uses. If one has been banned, Ryu rewrites the playlist to use one of the same site's other hosts, which serve the same files, and hands the corrected playlist to the player from a small server on 127.0.0.1. If an episode still can't be played, Ryu looks for the same show and episode on the other provider and plays it from there, saying which provider it came from.

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

1. Type a title in the search box and press Enter. Focus moves to the results list when the search finishes.
2. Press Enter on a result to load its episodes. Ryu says "Loading episodes", and focus moves to the episode list once they're in.
3. Press Enter on an episode, or use the Play button, to start playing it. Ryu says which episode it's loading, then the window switches to the player, focus lands on Pause, and the title names the episode.

The episode list only shows episodes that exist in the audio you picked. One Piece, for example, lists 1180 episodes when Subbed is chosen and 1155 when Dubbed is. Changing the audio choice refilters the list straight away, and the status bar shows how many subbed or dubbed episodes are left. The selection stays on the same episode when it's still listed, and otherwise moves to the closest earlier one. The Previous and Next keys in the player follow the same list, so they skip episodes that aren't available in that audio.

Alt+S, Alt+R, Alt+E, Alt+A and Alt+P jump to the search box, results, episodes, audio choice and Play button. Ctrl+P opens Preferences, from the player too.

Ryu stays in one window. The player replaces the search box and lists while an episode plays, and leaving the player brings them back.

In the player:

1. Space pauses and resumes, and Ryu says "Paused" or "Playing". When focus is on a button, Space presses that button instead.
2. Left and Right arrows seek 10 seconds, and Ryu speaks the new position. Shift with Left or Right seeks 60 seconds.
3. Up and Down arrows change the volume by 5, and Ryu speaks the new volume.
4. T speaks the current time, for example "3:21 of 24:10", without moving focus.
5. I skips the intro when the site says where it ends, and Ryu says "Intro" when the intro starts. Episodes without intro information have no Skip intro button, and I does nothing on them.
6. R turns reading subtitles aloud on or off.
7. N plays the next episode and P plays the previous one. On the last or first episode, Ryu says so instead.
8. Escape stops playback and goes back to the episode list, with the episode you were watching selected.
9. Alt+P is Pause or Play, Alt+B goes back 10 seconds, Alt+F goes forward 10 seconds, Alt+I presses Skip intro, Alt+R and Alt+N press Previous episode and Next episode, Alt+D toggles the Read subtitles aloud checkbox, and Alt+C closes the player.
10. Alt+T moves to the position slider, Alt+M to the time field, Alt+V to the volume slider and Alt+S to the subtitles choice. Arrow keys move the sliders and change the subtitle track when those have focus, and move the cursor in the time field.

The time field reads like "3:21 of 24:10" and updates once a second. The subtitles choice lists every track that loaded, with the site's default selected, plus Off.

When reading subtitles aloud is on, each new line of the selected subtitle track goes to your screen reader as it appears on screen. Lines queue behind each other instead of cutting each other off, and pausing or seeking keeps them in step with the video. Reading is on by default for subbed episodes and off for dubs, and Ryu remembers your choice for each separately. Turning subtitles Off in the subtitles choice also stops the reading.

Ryu says "End of episode" when playback reaches the end. If the clock hasn't moved for 15 seconds while you're not paused, Ryu says the video is not loading and the time field reads "Not loading". That usually means the site's video host for that episode is down, so try the other audio or another episode.

## Preferences

Preferences holds the provider, the provider's base URL, the preferred audio, and whether to try other providers when an episode won't play, which is on by default. The base URL is there for when a site moves to a new domain or you want a mirror, and each provider remembers its own. Reset to default puts back the built-in address. Settings are stored in `%APPDATA%\Ryu\settings.ini`.

## Command-line tool

`ryu-cli` drives the providers without the GUI and prints one plain line per item:

1. `ryu-cli providers`
2. `ryu-cli search "frieren"`
3. `ryu-cli episodes 481`
4. `ryu-cli streams 9227 dub`
5. `ryu-cli --provider kickassanime search "frieren"`, then `ryu-cli --provider kickassanime streams "sousou-no-frieren-2d15|1" sub`

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
