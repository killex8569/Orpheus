# Orpheus

Native music player for Linux, Windows, macOS and Android, in **C++ / Qt 6 (QML)**, with playback through **libmpv** (Qt Multimedia on Android), a smart local cache, and imports from YouTube and Spotify.

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

### Fedora (RPM package)

Download `orpheus-x.y.z-1.fc44.x86_64.rpm` from the [latest release](https://github.com/killex8569/Orpheus/releases/latest), then:

```bash
sudo dnf install ./orpheus-*.rpm     # pulls Qt, mpv, ffmpeg and Node.js from Fedora's repositories
orpheus                              # also in the applications menu
sudo dnf remove orpheus              # your music and playlists are kept
```

Built for Fedora 44. YouTube needs a JavaScript engine: Node.js is installed with the package (deno is used instead if present). To update, install the RPM of the new release the same way. Don't mix it with `./install.sh`: that one installs into `~/.local/bin`, which comes first in the `PATH`.

### Windows and macOS

Download the installer from the [latest release](https://github.com/killex8569/Orpheus/releases/latest). Everything is included (Qt, libmpv, ffmpeg, deno); yt-dlp is downloaded by Orpheus on first launch and kept up to date.

- **Windows 10/11 (x64)**: `Orpheus-x.y.z-windows-x64-setup.exe`. No administrator rights needed; installs into `%LOCALAPPDATA%\Programs\Orpheus`. Uninstall from "Installed apps". Windows SmartScreen may warn about an unknown publisher: "More info" → "Run anyway".
- **macOS 15+ (Apple Silicon)**: `Orpheus-x.y.z-macos-arm64.dmg`, then drag Orpheus into Applications. The app is not notarized by Apple: on first launch, open it, then go to System Settings → Privacy & Security → "Open Anyway".

To update, run the new installer: your tracks and playlists are kept (see [Data](#data) for where they live). No MPRIS outside Linux; media keys work while the window has focus.

### Android

Download `Orpheus-x.y.z-android-arm64.apk` from the [latest release](https://github.com/killex8569/Orpheus/releases/latest) on the phone, open it and allow the installation (Android 9 or later, 64-bit ARM). yt-dlp, Python, ffmpeg and a JavaScript engine (QuickJS) are bundled through [youtubedl-android](https://github.com/JunkFood02/youtubedl-android); the first launch unpacks them (a few seconds). yt-dlp is updated from Paramètres → yt-dlp.

On a phone, the sidebar becomes a drawer (☰), a tap plays a track and a long press opens its menu. Playback goes on in the background, with a notification and lock-screen and headset controls; the Back key goes back to the search, then sends Orpheus to the background while music plays.

Not available on Android: browser cookies (paste the Spotify `sp_dc` cookie in Paramètres instead; no "Mon compte YouTube"), a folder per playlist (files stay in the app's private storage), and the audio output settings.

Builds: the APK is built by the `android` job of `.github/workflows/installers.yml` (Qt 6.8 for Android, NDK r26b). To sign every release with the same key (needed to install an update over the previous version), create a keystore once and add three repository secrets:

```bash
keytool -genkeypair -keystore orpheus.jks -alias orpheus -keyalg RSA -keysize 2048 -validity 10000
base64 -w0 orpheus.jks   # → secret ANDROID_KEYSTORE_BASE64
# secrets ANDROID_KEYSTORE_PASSWORD (the keystore password) and ANDROID_KEY_ALIAS (orpheus)
```

Without them, the job signs with a throwaway key and says so: the APK installs, but the next one must be installed after uninstalling the previous one (which deletes the app's data).

The installers are built by `.github/workflows/installers.yml` (MSYS2 + Inno Setup on Windows, Homebrew + macdeployqt on macOS, `packaging/linux/orpheus.spec` in a Fedora container for the RPM). Each one is installed and launched on a clean system before the release. Pushing a `v*` tag attaches them to the release.

## Features

- **Search** on YouTube (default) or Spotify (selectable in the settings, or with the toggle next to the search bar). A pasted YouTube link plays directly. Tracks already on disk appear first ("Déjà en local").
- **Local cache** ("Cache local" playlist): every track you play is downloaded **once**, then played from disk. Before any search, Orpheus checks whether the track is already known: by YouTube id, by Spotify id, by an "artist|title" key, or through previously stored matches.
- **Personal playlists**: create, rename, reorder their tracks (right-click → Monter/Descendre), delete. Their tracks are never removed from the cache.
- **Sidebar**: the "Musique" and "Playlists" sections fold like Discord categories (click the title); a folded section still shows the open item and anything that needs attention (Jam in progress, update available). The state is remembered. Every playlist, "Cache local" included, can be reordered by drag and drop, or right-click → Monter/Descendre.
- **Folder per playlist**: on a playlist's page, "Choisir un dossier…" stores its tracks in a folder of your choice (another disk, your Music folder…), named `Artist - Title [id].opus`. Already downloaded tracks are moved there, and new ones follow. A track in several playlists that have a folder goes to the first of them in the sidebar order. "Par défaut" moves them back into Orpheus's library. Orpheus only moves its own files and never overwrites an existing one.
  To avoid duplicates, search results already in the playlist you are filling (the last one opened or added to) are greyed out, including other versions of the same title. In "Ajouter à une playlist", playlists that already contain the track are disabled.
- **Shuffle**: the "Aléatoire" button in a playlist is a toggle. When it is on, the **whole** playlist is shuffled. When it is off, the original order comes back and playback continues from the current track.
- **Imports**:
  - YouTube / YouTube Music playlist from a link, or from your account (cookies of the selected browser);
    if the browser's cookies can't be read (browser open, or Chromium-based browsers on Windows, which encrypt them), Orpheus says so once and carries on without them. On Windows, use Firefox;
  - Spotify playlist or album: public playlists without an account, in full; private ones and liked songs with your account, **free or Premium** (see [Connect Spotify](#connect-spotify-optional)).
  - From your accounts (Importer → "Mon compte YouTube" / "Mon compte Spotify"): tick the playlists you want, choose whether their tracks are downloaded for offline listening and, optionally, a folder (each playlist gets a sub-folder named after it). The selected playlists are imported one after the other; one that was already imported is marked as such and is synchronized.
  - Private Spotify playlist **without an account**: in the Spotify desktop app, select every track (`Ctrl+A`), copy (`Ctrl+C`), and paste into "Playlist Spotify privée : coller les morceaux". Track pages stay public even when the playlist is private. Pasting again under the same name updates the playlist.
    Each track is matched on YouTube with a score (title, artist, duration; live, cover, remix and similar versions are rejected).
  - "Synchroniser" adds new tracks without duplicates. Imported tracks also go into the "Cache local" playlist.
- **Lyrics** (`L` key, or the microphone button): synced lyrics from [LRCLIB](https://lrclib.net), free and with no account. The current line is highlighted, and clicking a line jumps to that part of the track. Plain-text lyrics are shown when no synced version exists. Lyrics are saved in the database, so they work **offline**: they are fetched automatically for every track kept locally, in the background, at the pace LRCLIB allows.
- **Jam** (shared listening on the local network): the host picks a name and a 4-digit code; friends join with the **name** and the code, with no IP address to type (an IP address still works as a fallback). Only synchronization goes over the network; everyone plays the track from their own disk. Playing a track that is already in the Jam jumps to it instead of adding a duplicate.
  Security: the code never travels in clear (challenge-response), 3 wrong codes block the device for the rest of the Jam, and the host only answers name lookups from its own local network, never from a blocked device, and at a limited rate. If two Jams answer to the same name, Orpheus refuses to pick one; creating a Jam checks that the name is free first.
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
   Exception: when you play a search result, the following results only get their stream prepared. Nothing is saved until you actually listen to them.

Other safeguards: queue resumes after a restart, automatic retries (10 s, then 40 s), a corrupted file is deleted and replaced by streaming, cache size limit (least recently played tracks go first, never the ones in your playlists), single instance.

## Connect Spotify (optional)

**Free account (recommended)**: Orpheus uses the session of the Spotify web player.

1. Log in to <https://open.spotify.com> in your browser.
2. Paramètres → Compte Spotify: choose that browser, then click "Se connecter". Orpheus reads the `sp_dc` session cookie through yt-dlp (the temporary copy of the browser's cookies is deleted right away); it stays on your computer and is only sent to Spotify.
   If the cookies can't be read (Chrome, Edge or Brave on Windows encrypt them), paste the value of the `sp_dc` cookie instead (F12 → Storage/Application → Cookies → `https://open.spotify.com`).

This is not an official API: the cookie is exchanged for a web player token (a TOTP code derived from a secret published in the web player), and playlists are read with the web player's GraphQL queries. When Spotify rotates the secret, Orpheus fetches the newer ones from a community list (`xyloflake/spot-secrets-go`); when it changes its queries, Orpheus reads their new fingerprints from the web player. A deeper change needs an Orpheus update. The cookie lasts about a year.

**Developer app (Premium)**: since February 2026, Spotify requires the owner of a developer app to have Premium (5 users per app). Under "Avec une application développeur Spotify":

1. Create an app at <https://developer.spotify.com/dashboard> (check "Web API").
2. Add the Redirect URI `http://127.0.0.1:8765/callback`.
3. Paste the Client ID, then click "Se connecter à Spotify". This connection is also needed to search on Spotify.

## yt-dlp

YouTube changes often. In Paramètres → yt-dlp, "Installer / mettre à jour yt-dlp" installs a copy managed by Orpheus, in the `bin/` folder of the [data folder](#data) (`yt-dlp.exe` on Windows). It is installed automatically on first launch, then updated at every launch.

## Data

Your tracks, playlists and settings live in a data folder of their own, never in the program folder: updating or uninstalling Orpheus leaves them alone (unless you answer "Yes" when the Windows uninstaller offers to delete them).

| System | Data folder | Program |
|---|---|---|
| Linux | `~/.local/share/orpheus/` | `~/.local/bin/orpheus` |
| Windows | `%LOCALAPPDATA%\orpheus\` (`C:\Users\<you>\AppData\Local\orpheus\`) | `%LOCALAPPDATA%\Programs\Orpheus\` |
| macOS | `~/Library/Application Support/orpheus/` | `/Applications/Orpheus.app` |
| Android | private app storage (`files/orpheus/`) | the app |

`ORPHEUS_HOME=<dir>` uses another data folder (any system).

Inside the data folder:

| Path | Contents |
|---|---|
| `orpheus.db` | SQLite: tracks, playlists, settings, imports, statistics |
| `library/` | audio files `<youtube-id>.opus` (embedded title, artist and album) |
| `covers/` | album covers |
| `bin/` | copy of yt-dlp managed by Orpheus |

Tracks of a playlist with its own folder (see "Folder per playlist") are stored in that folder instead of `library/`.

Hidden folders: on Windows, `AppData` is hidden: press `Win + R` and type `%LOCALAPPDATA%\orpheus`. On macOS, `~/Library` is hidden: in the Finder, `Cmd + Shift + G` and paste the path.

## Code

| File | Role |
|---|---|
| `src/Backend.*` | link to QML, track resolution (local first), imports |
| `src/Player.*` | queue, loading strategy |
| `src/AudioEngine*` | audio playback: libmpv (`AudioEngineMpv.cpp`), Qt Multimedia on Android (`AudioEngineQt.cpp`) |
| `src/Android*`, `android/` | Android: yt-dlp through youtubedl-android, notification and media controls, manifest and Gradle |
| `src/Downloader.*` | download queue: priority, spacing, retries, cache limit |
| `src/YtDlp.*` | async yt-dlp calls |
| `src/Spotify.*` | OAuth PKCE, Web API, public pages |
| `src/SpotifyWeb.*` | web player session (`sp_dc` cookie, TOTP token, GraphQL): free accounts, full public playlists |
| `src/Matcher.*` | normalization and Spotify → YouTube scoring |
| `src/Database.*` | SQLite schema and queries |
| `qml/` | interface (views, track list, player bar) |
| `tests/` | unit tests (`./build/orpheus_tests`) |

Development tools (environment variables): `ORPHEUS_HOME=<dir>` (separate data), `ORPHEUS_DEMO_SEARCH="query"` (searches and plays the first result), `ORPHEUS_DEMO_IMPORT=<url>`, `ORPHEUS_SCREENSHOT=out.png` (captures the window after `ORPHEUS_SCREENSHOT_DELAY` ms, then quits), `ORPHEUS_VIEW=jam` or `ORPHEUS_VIEW=playlist:<id>` (view shown at startup). On KDE, add `QT_FORCE_STDERR_LOGGING=1` to see the logs in the terminal.
