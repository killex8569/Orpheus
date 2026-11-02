# Orpheus

Native music player for Linux, in **C++ / Qt 6 (QML)**, with playback through **libmpv**, a smart local cache, and imports from YouTube and Spotify.

## Install

```bash
./install.sh      # compiles, runs the tests, installs into ~/.local/bin + applications menu
orpheus
./update.sh       # git pull, then rebuild and reinstall if anything changed (--force to always reinstall)
./uninstall.sh    # removes the program, icon and menu entry; your music and playlists are kept
```

Useful options: `install.sh --deps` installs the dependencies with the distribution's package manager (dnf, apt, pacman, zypper; uses sudo), `--no-tests` skips the unit tests, `--yes` answers yes to every question. `uninstall.sh --purge` also deletes the data (`~/.local/share/orpheus`), `--clean` deletes `build/`. Every script has `--help`.

Dependencies: Qt 6.5+ (`qt6-qtbase-devel`, `qt6-qtdeclarative-devel`, `qt6-qtsvg`), `mpv-devel`, `cmake`, `ninja`, `g++`, and at runtime `yt-dlp`, `ffmpeg` and `deno` (JavaScript engine required by YouTube).

Manual build: `cmake -S . -B build -G Ninja && cmake --build build && ./build/orpheus`

### Windows and macOS

Download the installer from the [latest release](https://github.com/killex8569/Orpheus/releases/latest). Everything is included (Qt, libmpv, ffmpeg, deno); yt-dlp is downloaded by Orpheus on first launch and kept up to date.

- **Windows 10/11 (x64)**: `Orpheus-x.y.z-windows-x64-setup.exe`. No administrator rights needed; installs into `%LOCALAPPDATA%\Programs\Orpheus`. Uninstall from "Installed apps". Windows SmartScreen may warn about an unknown publisher: "More info" → "Run anyway".
- **macOS 13+ (Apple Silicon)**: `Orpheus-x.y.z-macos-arm64.dmg`, then drag Orpheus into Applications. The app is not notarized by Apple: on first launch, open it, then go to System Settings → Privacy & Security → "Open Anyway".

To update, run the new installer: your tracks and playlists are kept (`%LOCALAPPDATA%\orpheus` on Windows, `~/Library/Application Support/orpheus` on macOS). No MPRIS outside Linux; media keys work while the window has focus.

The installers are built by `.github/workflows/installers.yml` (MSYS2 + Inno Setup on Windows, Homebrew + macdeployqt on macOS). Pushing a `v*` tag attaches them to the release.

## Features

- **Search** on YouTube (default) or Spotify (selectable in the settings, or with the toggle next to the search bar). A pasted YouTube link plays directly. Tracks already on disk appear first ("Déjà en local").
- **Local cache** ("Cache local" playlist): every track you play is downloaded **once**, then played from disk. Before any search, Orpheus checks whether the track is already known: by YouTube id, by Spotify id, by an "artist|title" key, or through previously stored matches.
- **Personal playlists**: create, rename, reorder (right-click → Monter/Descendre), delete. Their tracks are never removed from the cache.
  To avoid duplicates, search results already in the playlist you are filling (the last one opened or added to) are greyed out, including other versions of the same title. In "Ajouter à une playlist", playlists that already contain the track are disabled.
- **Shuffle**: the "Aléatoire" button in a playlist is a toggle. When it is on, the **whole** playlist is shuffled. When it is off, the original order comes back and playback continues from the current track.
- **Imports**:
  - YouTube / YouTube Music playlist from a link, or from your account (cookies of the selected browser);
  - Spotify playlist or album: public ones without an account (≈100 tracks); private ones and liked songs with an account (OAuth, no secret).
  - Private Spotify playlist **without an account**: in the Spotify desktop app, select every track (`Ctrl+A`), copy (`Ctrl+C`), and paste into "Playlist Spotify privée : coller les morceaux". Track pages stay public even when the playlist is private. Pasting again under the same name updates the playlist.
    Each track is matched on YouTube with a score (title, artist, duration; live, cover, remix and similar versions are rejected).
  - "Synchroniser" adds new tracks without duplicates. Imported tracks also go into the "Cache local" playlist.
- **Lyrics** (`L` key, or the microphone button): synced lyrics from [LRCLIB](https://lrclib.net), free and with no account. The current line is highlighted, and clicking a line jumps to that part of the track. Plain-text lyrics are shown when no synced version exists. Lyrics are saved in the database, so they work **offline**: they are fetched automatically for every track kept locally, in the background, at the pace LRCLIB allows.
- **Statistics**: a play counts once **more than half** of the track has really been listened to (pauses and seeks excluded). The view shows listening time, plays, distinct tracks and artists, a top artists list (grouped regardless of case or "feat."), and top tracks, over 7 days, 30 days, 12 months, or all time.
- **Quality**: the best audio stream (Opus ~130-160 kb/s, 256 kb/s with YouTube Premium) is kept without re-encoding. Exclusive (bit-perfect) mode is available. Note: LDAC is a Bluetooth codec, not a file quality setting.
- **Shortcuts**: `Space` play/pause, `Ctrl+K` search, `←/→` ±10 s, `Shift+←/→` previous/next, `↑/↓` volume, `S` shuffle, `R` repeat, multimedia keys.

## How a track loads

1. Local file → plays immediately.
2. Otherwise a **priority download** (one transfer, ~3-5 s depending on YouTube), then playback from disk.
   Background downloads pause meanwhile. Otherwise, 2 run in parallel (configurable from 1 to 3), with spaced-out starts.
   Each track takes ~8-10 s, mostly spent waiting on YouTube: ~1 s for yt-dlp startup, ~3 s for extraction and JavaScript solving, then a few seconds of mandatory delay before the stream.
3. If the download fails or takes longer than 12 s, playback switches automatically to streaming. The cache will be retried later.
4. The next track in the queue is downloaded during playback, so the transition is instant.

Other safeguards: queue resumes after a restart, automatic retries (10 s, then 40 s), a corrupted file is deleted and replaced by streaming, cache size limit (least recently played tracks go first, never the ones in your playlists), single instance.

## Connect Spotify (optional)

1. Create an app at <https://developer.spotify.com/dashboard> (check "Web API").
2. Add the Redirect URI `http://127.0.0.1:8765/callback`.
3. Paramètres → Compte Spotify: paste the Client ID, then click "Se connecter".

## yt-dlp

YouTube changes often. In Paramètres → yt-dlp, "Installer / mettre à jour yt-dlp" installs a copy managed by Orpheus (`~/.local/share/orpheus/bin/yt-dlp`), which is then updated automatically at every launch.

## Data

`~/.local/share/orpheus/` (or `$ORPHEUS_HOME`):

| Path | Contents |
|---|---|
| `orpheus.db` | SQLite: tracks, playlists, settings, imports |
| `library/` | audio files `<youtube-id>.opus` (embedded metadata) |
| `covers/` | album covers |
| `bin/` | copy of yt-dlp managed by Orpheus (optional) |

## Code

| File | Role |
|---|---|
| `src/Backend.*` | link to QML, track resolution (local first), imports |
| `src/Player.*` | libmpv, queue, loading strategy |
| `src/Downloader.*` | download queue: priority, spacing, retries, cache limit |
| `src/YtDlp.*` | async yt-dlp calls |
| `src/Spotify.*` | OAuth PKCE, Web API, public pages |
| `src/Matcher.*` | normalization and Spotify → YouTube scoring |
| `src/Database.*` | SQLite schema and queries |
| `qml/` | interface (views, track list, player bar) |
| `tests/` | unit tests (`./build/orpheus_tests`) |

Development tools (environment variables): `ORPHEUS_HOME=<dir>` (separate data), `ORPHEUS_DEMO_SEARCH="query"` (searches and plays the first result), `ORPHEUS_DEMO_IMPORT=<url>`, `ORPHEUS_SCREENSHOT=out.png` (captures the window after `ORPHEUS_SCREENSHOT_DELAY` ms, then quits). On KDE, add `QT_FORCE_STDERR_LOGGING=1` to see the logs in the terminal.
test
test
test
test
