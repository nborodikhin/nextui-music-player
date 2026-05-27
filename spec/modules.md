# Spec: Feature Modules

## Menu Module (`module_menu.{c,h}`, `ui_main.{c,h}`)

**Entry**: `MenuModule_run(screen)` → returns `MENU_*` constant or `MENU_QUIT`

Menu items (in order):
- `MENU_RESUME` (0) / `MENU_NOW_PLAYING` (0) — same slot, dynamically labeled
- `MENU_LIBRARY` (1)
- `MENU_RADIO` (2)
- `MENU_PODCAST` (3)
- `MENU_SETTINGS` (4)

When `Background_isPlaying()` is true, slot 0 shows "Now Playing" and routes to the active background module. Otherwise slot 0 shows "Resume: <track name>" from `Resume_getLabel()`.

`MenuModule_setToast(msg)` lets other modules post a one-line message shown on return to menu.

## Library Module (`module_library.{c,h}`)

File browser rooted at the device's `Music/` folder. Uses the `browser.{c,h}` widget for directory navigation. Selecting an audio file:
1. Builds `PlaylistContext` from the directory (recursive scan)
2. Launches `PlayerModule_run()`

Supports filtering to audio formats only (`Player_detectFormat()`).

**Cursor preservation**: when pressing B to exit a subdirectory, the browser returns to the parent directory with the cursor positioned on the subdirectory that was just exited (not reset to index 0).

Also exposes "Download Music" entry → launches downloader sub-flow.

## Player Module (`module_player.{c,h}`, `ui_music.{c,h}`)

**Entry**: `PlayerModule_run(screen)` or `PlayerModule_runResume(screen, rs)`

Owns:
- `PlaylistContext` (current queue)
- Shuffle / repeat state
- Spectrum visibility + style cycling
- Lyrics display
- Waveform progress bar
- Album art display

**Controls** (per `README.md`):
- A: Play/Pause
- X: Shuffle toggle
- Y: Repeat toggle
- D-Pad Up/Down: Next/Prev track
- D-Pad Left/Right: Rewind/Fast-forward
- L1/R1: Prev/Next track (alternative)
- L2/L3: Cycle spectrum visualizer
- R2/R3: Toggle lyrics
- Select: Screen off

Saves resume state via `Resume_saveFiles()` / `Resume_savePlaylist()` on each track change and periodically during playback.

Background playback: pressing B sets `Background_setActive(BG_MUSIC)` and returns `MODULE_EXIT_TO_MENU` without stopping audio.

## Radio Module (`module_radio.{c,h}`, `ui_radio.{c,h}`)

**Entry**: `RadioModule_run(screen)`

Views (internal state machine):
1. **Station list** — user's preset stations (loaded from `stations.txt`)
2. **Curated browser** — country picker → station list from bundled JSON
3. **Now playing** — ICY metadata, album art, buffer level bar

**Controls**:
- D-Pad Up/Down or L1/R1: Next/Prev station
- Y: Station management menu (add/remove/save)
- B: Stop + back to menu (background playback stays active)

## Podcast Module (`module_podcast.{c,h}`, `ui_podcast.{c,h}`)

**Entry**: `PodcastModule_run(screen)`

Views (internal state machine):
1. **Home** — "Continue Listening" + subscriptions list
2. **Subscription list** — list of subscribed feeds
3. **Episode list** — episodes for selected feed (paginated, `PODCAST_EPISODE_PAGE_SIZE = 50`)
4. **Search** — iTunes keyword search
5. **Top Charts** — Apple Podcast Charts by country
6. **Playback** — active episode, progress

Features:
- RSS parse via `podcast_rss.c` (yxml-based)
- Async search via `podcast_search.c` (iTunes API)
- Download queue with background downloader thread
- Progress tracking per episode GUID → resume position

**Controls**:
- Y: Subscription management (subscribe/unsubscribe)
- A: Select (play downloaded, or download pending)
- B: Back / stop + background

## Downloader Module (`module_downloader.{c,h}`, `ui_downloader.{c,h}`)

**Entry**: launched from Library module

Wraps `downloader.{c,h}` which shells out to the bundled `bin/yt-dlp` binary.

Views:
1. **Search** — keyboard input → async YouTube Music search
2. **Results** — list of `DownloaderResult` (title, artist, duration)
3. **Queue** — pending/downloading/complete items
4. **yt-dlp updater** — version check + update progress

Downloads land in `Music/Download/` on the SD card.

## Settings Module (`module_settings.{c,h}`, `ui_settings.{c,h}`)

**Entry**: `SettingsModule_run(screen)`

Settings managed:
- **Screen off timeout**: 60s / 90s / 120s / Off (cycles with D-Pad)
- **Lyrics enabled**: on/off toggle
- **Speaker bass filter**: high-pass cutoff Hz (0 = off)
- **Soft limiter**: off / mild / medium / strong (protects speakers from clipping)

Up/Down navigation wraps at both ends (consistent with all other menus in the app).

All settings auto-save to `settings.json` on change via `Settings_save()`.

## System Module (`module_system.{c,h}`, `ui_system.{c,h}`)

**Entry**: `SystemModule_run(screen)`

Shows:
- App version (`SelfUpdate_getVersion()`)
- Self-update flow (check → download → extract → apply)
- QR code for project page (`qr_code_data.h` — embedded bitmap)
- License/attribution info

## Common Module (`module_common.{c,h}`)

Not a standalone module — a shared service used by all modules.

Key responsibilities:
- `ModuleCommon_handleGlobalInput()` — called at top of every module's input loop
- Screen-off hint countdown + backlight control
- Auto screen-off idle detection
- Toast message lifecycle (`ModuleCommon_tickToast()`)
- Volume overlay via `ModuleCommon_PWR_update()`
- USB HID volume events via `ModuleCommon_handleHIDVolume()`
