# Spec: Application UI — Screens, Modes, and Transitions

## Screen Inventory

```
Splash
└── Main Menu
    ├── [Resume / Now Playing] ─────────────────────► Music Player
    ├── Library
    │   ├── Files (File Browser) ───────────────────► Music Player
    │   ├── Playlists
    │   │   ├── Playlist List
    │   │   └── Playlist Detail ─────────────────────► Music Player
    │   └── Downloader
    │       ├── Downloader Menu
    │       ├── Downloader Searching
    │       ├── Downloader Results
    │       └── Downloader Queue
    ├── Online Radio
    │   ├── Station List ────────────────────────────► Radio Player
    │   ├── Country Browser
    │   ├── Station Browser
    │   └── Manual Setup Help
    ├── Podcasts
    │   ├── Podcast Home
    │   ├── Manage Podcasts
    │   ├── Top Shows
    │   ├── Search Results
    │   ├── Episode List ────────────────────────────► Podcast Player
    │   └── Download Queue
    ├── [Send Crash Report] ─────────────────────────► Crash Report Dialog (overlay, conditional)
    └── Settings
        ├── Settings Menu
        ├── About
        ├── App Updating
        └── yt-dlp Updating
```

---

## Screen Modes

These modes apply on top of any screen that has active audio playback (Music Player, Radio Player, Podcast Player). They are independent of which screen is currently rendered.

### Normal Mode
Full UI is visible. Backlight is on.

### Screen-Off Hint Mode
Triggered by:
- **Select tap** (while in a player screen)
- **Auto screen-off timeout** (configurable: 60s / 90s / 120s / Off since last input)

What the user sees:
- Black screen
- Centered text: `"Press SELECT + A to wake screen"`
- Countdown: 4 seconds (`SCREEN_OFF_HINT_DURATION_MS`)

While hint is active:
- Any button press **resets** the 4-second countdown (hint shows again from full duration)
- **SELECT + A** → immediately returns to Normal Mode
- Timeout expires (4s with no buttons) → transitions to Screen-Off Mode

### Screen-Off Mode
Backlight is turned off. Audio continues unaffected.

What the user sees: nothing (black screen, backlight off).

While screen is off:
- Any button press → backlight turns on, Screen-Off Hint Mode starts again
- Audio continues; track auto-advance still works

### Global Overlay Mode
These overlays render over any current screen without changing the underlying state.

**Volume overlay** (platform-managed)
- Triggered by: hardware Volume +/- buttons
- Shows a bar for volume, brightness (MENU+Vol), or color temperature (SELECT+Vol)
- Auto-hides 800ms after button released, then fully hidden after another 500ms

**Controls Help dialog** (triggered by START short press, any screen)
- Shows context-sensitive button mapping for the current screen
- Any button press closes it

**Quit Confirmation dialog** (triggered by START long press ≥ 500ms, any screen)
- "Quit Music Player?" / "A: Yes   B: No"
- A → exits the app entirely (background audio stops)
- B or START → dismisses, returns to current screen

---

## Screens

### Splash Screen
**Shown**: on startup, before any interactive screen appears.

What the user sees:
- Black background
- `"Music Player"` centered vertically (slightly above center)
- `"Loading..."` just below center, in gray

Duration: time taken to initialize all subsystems (Player_init, icon loading, network init, etc.). Typically 1–3 seconds on device.

Transitions:
- → **Main Menu** automatically when initialization completes

---

### Main Menu
Three visual variants — see `spec/menu-behavior.md` for the full state machine.

What the user sees:
- Title bar: `"Music Player"`
- Item list (4 or 5 items depending on state, +1 if a crash report is pending):
  - **State A** (no first item): Library / Online Radio / Podcasts / Settings
  - **State B** (resume): Resume: \<track name\> / Library / Online Radio / Podcasts / Settings
  - **State C** (now playing): Now Playing: \<Music|Radio|Podcast\> / Library / Online Radio / Podcasts / Settings
  - When a non-skipped crash report exists and `Settings: Collect crash reports = Yes`, a `Send Crash Report` row is inserted immediately above `Settings`. See `spec/crash-reporting.md` and `spec/menu-behavior.md`.
- Selected item highlighted
- Footer: `B: EXIT`
- Toast at bottom (3s, from returning sub-modules or from the exit-confirm prompt)
- Update badge on Settings if update is available: `"Settings (Update available)"`

Transitions (A button):
- Resume / Now Playing → **Music Player** or **Radio Player** or **Podcast Player** (depending on active background type)
- Library → **Library Menu**
- Online Radio → **Radio Station List**
- Podcasts → **Podcast Home**
- Send Crash Report (conditional) → **Crash Report Dialog** (overlay)
- Settings → **Settings Menu**

B button → two-step exit. First press shows toast `"Press B again to exit"`. Second B-press within 3 s shows `"Exiting..."`, then the app exits ~100 ms later. Other buttons during the window behave normally and do not cancel the arm. After 3 s the toast clears and the next B press is treated as a fresh first press. See `spec/menu-behavior.md` for the full state machine.

---

### Library Menu
What the user sees:
- Title bar: `"Library"`
- 3 items: Files / Playlists / Downloader
- Footer: `B: BACK`

Transitions:
- Files → **File Browser**
- Playlists → **Playlist List**
- Downloader → **Downloader Menu** (or toast "Internet connection required" / "Downloader not available" if unavailable)
- B → **Main Menu**

---

### File Browser
What the user sees:
- Title bar: current folder name (truncated if long)
- List of entries: subdirectories and audio files, each with a file-type icon
- `▶ Play All` entry at the top of each folder containing audio files
- Currently playing file highlighted (if audio is running in background)
- Footer: `A: PLAY   B: BACK`
- "Add to Playlist" dialog (overlay) when Y is pressed on an audio file or directory

Controls:
- A on folder → navigate into subfolder
- A on audio file → builds directory queue, starts playback → **Music Player**
- A on "Play All" → builds queue from all files alphabetically → **Music Player**
- Y on audio file → "Add to Playlist" overlay (shows "Add to Playlist: (1 file)")
- Y on directory → collects all playable files recursively (max 1000) → "Add to Playlist" overlay (shows "Add to Playlist: (N files)")
- X on audio file → delete confirmation dialog
- B at root (Music/) → back to **Library Menu**
- B in subfolder → navigate up one level; cursor is placed on the subdirectory just exited

---

### Music Player
What the user sees:
- **Top area**: track title (scrolls if long), artist, album
- **Left/center**: album art (embedded or fetched from iTunes; placeholder if none)
- **Progress area**: waveform overview bar with playhead, current time / total time
- **Status row**: Shuffle indicator (active when on) / Repeat indicator (active when on) / Track N of M (when in a playlist)
- **Spectrum visualizer** (when enabled): animated frequency bars in one of 4 color styles, rendered as GPU overlay; hidden when off
- **Lyrics** (when enabled and fetched): current line highlighted, scrolling with playback, GPU overlay
- Footer: `A: PLAY/PAUSE   B: BACK`

Screen modes available here: Normal, Screen-Off Hint, Screen-Off.

Transitions:
- B (while playing) → audio continues in background, returns to **File Browser** (or **Playlist Detail** if launched from there), Main Menu shows "Now Playing"
- B (while paused) → stops audio, returns to **File Browser** (or **Playlist Detail**)
- Track ends naturally → next track loads automatically (or playlist ends → back to **File Browser** / **Playlist Detail**)
- START long press → Quit Confirmation

---

### Playlist List
What the user sees:
- Title bar: `"Playlists"`
- List of saved M3U playlists, each showing name and track count: `"My Mix (12)"`
- Empty state if no playlists exist
- Footer: `A: OPEN   Y: NEW   X: DELETE   B: BACK`
- Toast confirmation for create/delete actions

Transitions:
- A → **Playlist Detail** for selected playlist
- Y → on-screen keyboard → creates new playlist → stays on Playlist List
- X → delete confirmation → stays on Playlist List
- B → **Library Menu**

---

### Playlist Detail
What the user sees:
- Title bar: playlist name
- List of tracks (audio filename, no path)
- Empty state if playlist has no tracks (or all files missing)
- Footer: `A: PLAY   X: REMOVE   B: BACK`
- Toast for remove action

Transitions:
- A → **Music Player** starting from selected track
- X → remove confirmation → stays on Playlist Detail
- B → **Playlist List** (refreshes track counts)

---

### Radio Station List
What the user sees:
- Title bar: `"Online Radio"`
- List of user's preset stations (name + genre/slogan if set)
- Empty state if no stations saved (with hint to add via Y)
- Footer: `A: PLAY   Y: MANAGE   X: DELETE   B: BACK`
- Toast for add/delete actions

Transitions:
- A → if WiFi available: starts stream → **Radio Player** / if no WiFi: toast "Internet connection required"
- Y → **Country Browser**
- X → delete confirmation → stays on Station List
- B → **Main Menu**

---

### Radio Player
What the user sees:
- **Station name** (large)
- **Status**: Connecting… / Buffering… / Playing (with buffer fill bar)
- **Metadata**: current song title and artist (from ICY stream, updates in real-time)
- **Album art**: fetched from iTunes based on current song metadata; updates when song changes; placeholder while fetching or unavailable
- **Error state**: error message if stream failed

Screen modes available here: Normal, Screen-Off Hint, Screen-Off.

Transitions:
- Up / R1 → next station (wraps around), reconnects
- Down / L1 → previous station (wraps around), reconnects
- A → toggle stop/start
- B (while playing) → audio continues in background → **Radio Station List**, Main Menu shows "Now Playing: Radio"
- B (while stopped/error) → **Radio Station List**, no background
- START long press → Quit Confirmation

---

### Country Browser (Radio)
What the user sees:
- Title bar: `"Add Station"`
- List of available countries (e.g. Malaysia, Singapore, USA)
- Footer: `A: SELECT   Y: MANUAL SETUP   B: BACK`

Transitions:
- A → **Station Browser** for selected country
- Y → **Manual Setup Help**
- B → **Radio Station List**

---

### Station Browser (Radio)
What the user sees:
- Title bar: country name
- Alphabetically sorted list of curated stations
- Each station shows a checkmark (or indicator) if already in the user's station list
- Toast for add/remove actions
- Footer: `A: ADD/REMOVE   Y: MANUAL SETUP   B: BACK`

Station already in list:
- A → confirmation dialog "Remove Station?" → A: removes, B: cancels

Station not in list:
- A → adds immediately → toast "Added: \<name\>"; if already at 32-station limit: toast "Maximum 32 stations reached"

Transitions:
- Y → **Manual Setup Help** (returns here on B)
- B → **Country Browser**

---

### Manual Setup Help (Radio)
What the user sees:
- Scrollable text instructions for adding stations by editing `stations.txt` on the SD card
- Up / Down to scroll
- Footer: `B: BACK`

Transitions:
- B → whichever screen opened Help (**Country Browser** or **Station Browser**)

---

### Podcast Home
What the user sees:
- **Continue Listening** section (up to 2 recent in-progress episodes, with artwork, title, podcast name, progress %)
- **Subscriptions** list (all subscribed podcasts, with artwork, unread count badge if new episodes)
- Y → Manage Podcasts
- X on subscription → unsubscribe confirmation
- A on Continue Listening entry → resumes that episode → **Podcast Player**
- A on subscription → **Episode List**
- Footer: `A: SELECT   Y: MANAGE   X: UNSUB   B: BACK`

Transitions:
- A (Continue Listening) → **Podcast Player** at saved position
- A (subscription) → **Episode List**
- Y → **Manage Podcasts**
- B → **Main Menu**

---

### Manage Podcasts
What the user sees:
- Menu: Search / Top Shows / (optionally: Subscribe by URL)
- Footer: `B: BACK`

Transitions:
- Search → opens keyboard → **Podcast Search Results**
- Top Shows → **Podcast Top Shows**
- B → **Podcast Home**

---

### Podcast Top Shows
What the user sees:
- Title bar: `"Top Shows"` (country-specific)
- List of charting podcasts (artwork, name, genre)
- Subscribed indicator per item
- A → subscribe / unsubscribe toggle
- X → refresh chart (re-fetches from Apple)
- Footer: `A: SUBSCRIBE   X: REFRESH   B: BACK`

Loading state: spinner while chart is fetching.

Transitions:
- B → **Manage Podcasts**

---

### Podcast Search Results
What the user sees:
- Title bar: `"Search: <query>"`
- List of search results (artwork, name, author)
- Subscribed indicator per item
- A → subscribe / unsubscribe toggle
- Loading state: spinner while searching
- Footer: `A: SUBSCRIBE   B: BACK`

Transitions:
- B → **Manage Podcasts**

---

### Episode List
What the user sees:
- Title bar: podcast name
- Episode list, most recent first
- Each row shows: title, date, duration, new badge (if unread), download status (not downloaded / downloading N% / downloaded)
- A on downloaded episode → **Podcast Player**
- A on not-downloaded episode → queues download, shows progress in-place
- Y → refresh episodes from RSS feed
- X → mark episode as played / unplayed
- Footer: `A: PLAY/DOWNLOAD   Y: REFRESH   X: MARK   B: BACK`

Transitions:
- A (downloaded) → **Podcast Player**
- B → **Podcast Home**

---

### Podcast Player
What the user sees:
- **Episode title** (scrolls if long)
- **Podcast name** (smaller, below title)
- **Album art** / podcast artwork
- **Progress bar** (with current position and total duration)
- **Playback controls**: play/pause state visible
- **Playback speed indicator** (if not 1.0×)

Screen modes available here: Normal, Screen-Off Hint, Screen-Off.

Controls:
- A → play / pause
- Left → rewind 10 seconds
- Right → fast-forward 30 seconds
- B (while playing) → audio continues in background → **Episode List**, Main Menu shows "Now Playing: Podcast"
- B (while paused) → stops, returns to **Episode List**

Transitions:
- Episode ends → **Episode List**
- START long press → Quit Confirmation

---

### Downloader Menu
What the user sees:
- Title bar: `"Downloader"` or `"YouTube Music"`
- 2 items: `Search YouTube Music` / `Download Queue`
- Footer: `B: BACK`

Transitions:
- Search → opens on-screen keyboard → **Downloader Searching** (while query runs) → **Downloader Results**
- Download Queue → **Downloader Queue**
- B → **Library Menu**

---

### Downloader Searching
What the user sees:
- "Searching…" spinner / status message
- B → cancel and return to **Downloader Menu**

Transitions:
- Search completes → **Downloader Results**
- Search fails → **Downloader Menu** with error toast
- B → **Downloader Menu**

---

### Downloader Results
What the user sees:
- List of search results: title, artist, duration
- A → add selected item to download queue → toast "Added to queue"
- Footer: `A: ADD TO QUEUE   B: BACK`

Transitions:
- B → **Downloader Menu**

---

### Downloader Queue
What the user sees:
- List of queued/downloading/completed items
- Each row shows title and status: Pending / Downloading N% / Complete / Failed
- Active download shows speed and ETA
- A → start downloading (if queue is idle)
- X → remove selected item from queue
- Footer: `B: BACK`

Transitions:
- B → **Downloader Menu**

---

### Settings Menu
What the user sees:
- Title bar: `"Settings"`
- 7 items with current values shown inline:

| Item | Values | Change |
|------|--------|--------|
| Screen Off | 60s / 90s / 120s / Off | Left / Right / A cycle |
| Bass Filter | Off / 80Hz / 100Hz / 120Hz | Left / Right / A cycle |
| Soft Limiter | Off / Mild / Medium / Strong | Left / Right / A cycle |
| Collect Crash Reports | Yes / No | Left / Right / A toggle |
| Clear Cache | — (action) | A → confirmation |
| Update yt-dlp | — (action) | A → **yt-dlp Updating** |
| About | — (action) | A → **About** |

- Navigation: Up/Down moves cursor; Left/Right cycles value for the first three items, pages list for the last three
- Footer: `B: BACK`

Transitions:
- Clear Cache → inline confirmation dialog → Settings Menu
- Update yt-dlp → **yt-dlp Updating**
- About → **About**
- B → **Main Menu**

---

### About
What the user sees:
- App version: `v1.x.x`
- Update status: checking… / "Up to date" / "Update available: vX.Y.Z"
- Release notes snippet (when update available)
- QR code linking to the project page
- A when update available → **App Updating**
- A when no update / checking → re-checks for update (requires WiFi)
- Footer: `B: BACK`

Transitions:
- A (update available) → **App Updating**
- B → **Settings Menu**

---

### App Updating
What the user sees:
- Progress: Downloading… / Extracting… / Applying…
- Progress percentage and `"X.X MB / Y.Y MB"` detail
- A (when complete) → quits app to apply update
- B (while downloading) → cancels update → **About**
- Footer: `A: APPLY   B: CANCEL` (context-sensitive)

Transitions:
- Complete + A → app exits to apply update
- B → **About**

---

### yt-dlp Updating
What the user sees:
- yt-dlp update progress (download percentage)
- B → cancel → **Settings Menu**

Transitions:
- Complete → **Settings Menu**
- B → **Settings Menu**

---

## Navigation Model

The UI is structured as a stack of screens. **B always pops the top screen off the stack**, returning to whatever pushed it. This applies uniformly across the app — the player, the playlist manager, the radio module, dialogs, and modal overlays all follow the same rule.

### What is a stack frame

Anything navigational is on the stack:

- Top-level screens entered from the Main Menu (Library, Online Radio, Podcasts, Settings, Music Player via Resume / Now Playing)
- Sub-screens within a module (Playlist List, Playlist Detail, Episode List, etc.)
- Internal states within the Music Player module:
  - File Browser state (pushed when entering Music Player from Library → Files)
  - Playing state (pushed when A is pressed on a file or "Play All")
- Directory navigation in the File Browser — each "A on folder" pushes a frame onto the file browser's internal nav stack; B pops back to the parent with the cursor restored on the folder you came from
- Modal dialogs and overlays:
  - Confirmation dialogs ("Delete Playlist?", "Remove Track?", "Quit?")
  - The Add-to-Playlist dialog
  - The Crash Report dialog (opened from the Main Menu's conditional `Send Crash Report` row)
  - The on-screen keyboard
  - Controls Help

### What is NOT on the stack

These are not navigation — B does not interact with them:

- **Toasts** — non-interactive, auto-dismiss after 3s
- **Setting toggles within the player** — shuffle (X), repeat (Y), spectrum visualizer (L2/L3), lyrics (R2/R3); these change state but don't push a screen
- **Track navigation within a player** — Up / Down / L1 / R1 navigate inside the current track list; the player screen itself stays on top
- **Screen-Off Hint Mode and Screen-Off Mode** — these are a power state machine layered over whatever player screen is currently on top of the stack. They are dismissed by SELECT+A or by the timeout returning to the underlying player; B is not part of their state machine.
- **Hardware-setting overlays** — Volume, Brightness, Color-temp; auto-hide on release

### "Entered from" semantics for the Music Player

The Music Player module is special because it can be reached from multiple entry points, and B must pop back to the correct one:

| Entered from | B (while playing) pops to |
|---|---|
| Main Menu → Resume | Main Menu |
| Main Menu → Now Playing | Main Menu |
| Main Menu → Library → Files (File Browser → A on file) | File Browser (audio continues in background) |
| Main Menu → Library → Playlists → Playlist Detail → A | Playlist Detail (audio continues in background) |

When B is pressed while paused (rather than playing), playback is fully stopped (no background audio) and the same pop happens — the destination only depends on where the player was entered from, not on the player's state.

### Natural end-of-playlist

When the last track finishes and neither repeat nor shuffle starts another, the same pop happens automatically — the player exits to the screen that pushed it, just as if B had been pressed.

### B at the root of the stack (Main Menu)

There is no screen below the Main Menu — popping it means exiting the app. To prevent accidental exits, the Main Menu uses a two-step B gesture: the first press shows a 3-second toast `"Press B again to exit"`, and a second B-press while that toast is visible shows `"Exiting..."` and exits the app ~100 ms later, giving the user visual confirmation before NextUI's own UI takes over. Other buttons during the window behave normally and do not cancel the arm. See `spec/menu-behavior.md` for details.

---

## Global Transition Summary

```
Any screen
  ├── START short press ──────► Controls Help overlay (any button → dismiss)
  ├── START long press ───────► Quit Confirmation overlay
  │                                ├── A ──────────────────► App exits
  │                                └── B or START ─────────► back to same screen
  ├── Vol+/Vol- buttons ──────► Volume overlay (auto-hides)
  └── MENU+Vol / SELECT+Vol ──► Brightness / color-temp overlay

Any player screen (music / radio / podcast)
  ├── Select tap ─────────────► Screen-Off Hint Mode
  │   ├── Any button ─────────► resets hint timer
  │   ├── SELECT+A ───────────► Normal Mode
  │   └── 4s timeout ─────────► Screen-Off Mode
  │       └── Any button ─────► Screen-Off Hint Mode
  └── Auto timeout (60/90/120s idle) ─► same as Select tap above
```

---

## Controls Help State Reference

Every screen passes an `app_state` integer to `render_controls_help()` (triggered by START short press). This table is the authoritative mapping from screen to the bindings shown in the dialog.

| app_state | Screen | Bindings shown |
|-----------|--------|----------------|
| 0 | Main Menu | Up/Down/Left/Right nav, X clear, Start hold exit |
| 1 | File Browser | Up/Down/Left/Right nav, Y add to playlist, X delete, Start hold exit |
| 2 | Music Player | X shuffle, Y repeat, Up/R1 next, Down/L1 prev, Left/Right seek, L2/L3 visualizer, R2/R3 lyrics, Select screen off, Select+A wake, Start hold exit |
| 3 | Radio Station List | Up/Down/Left/Right nav, Y manage, X delete, Start hold exit |
| 4 | Radio Player | Up/R1 next station, Down/L1 prev station, Select screen off, Select+A wake, Start hold exit |
| 5 | Country Browser | Up/Down/Left/Right nav, Y manual help, Start hold exit |
| 6 | Station Browser | Up/Down/Left/Right nav, A add/remove, Y manual help, Start hold exit |
| 16 | Downloader Menu | Up/Down/Left/Right nav, Start hold exit |
| 18 | Downloader Results | Up/Down/Left/Right nav, B back, Start hold exit |
| 19 | Downloader Queue | Up/Down/Left/Right nav, Start hold exit |
| 23 | About | Start hold exit |
| 30 | Podcast Home | Up/Down/Left/Right nav, X unsubscribe, Y manage, Start hold exit |
| 31 | Manage Podcasts | Up/Down/Left/Right nav, Start hold exit |
| 32 | Subscriptions | Up/Down/Left/Right nav, X unsubscribe, Start hold exit |
| 33 | Top Shows | Up/Down/Left/Right nav, A subscribe/unsub, X refresh, Start hold exit |
| 34 | Podcast Search Results | Up/Down/Left/Right nav, A subscribe/unsub, Start hold exit |
| 35 | Episode List | Up/Down/Left/Right nav, Y refresh, X mark played, Start hold exit |
| 36 | Podcast Buffering | Start hold exit |
| 37 | Podcast Player | Left rewind 10s, Right forward 30s, Select screen off, Select+A wake, Start hold exit |
| 40 | Settings Menu | Up/Down/Left/Right nav/change, Start hold exit |
| 41 | Settings About | Start hold exit |
| 50 | Playlist List | Up/Down/Left/Right nav, X delete playlist, Start hold exit |
| 51 | Playlist Tracks | Up/Down/Left/Right nav, X remove track, Start hold exit |
| 55 | Library Menu | Up/Down/Left/Right nav, Start hold exit |
| (default) | Any other | Start hold exit |

---

## Screen Flow for New User (Golden Path)

1. **Splash** → "Music Player" + "Loading..."
2. **Main Menu** → Library selected (index 0 since no resume/now-playing)
3. **Library Menu** → Files
4. **File Browser** → navigate to Music folder → select a track
5. **Music Player** → track plays, waveform shows, album art loads
6. B press → audio goes to background, back to **File Browser**
7. B press → back to **Library Menu**
8. B press → back to **Main Menu** → "Now Playing: Music" appears at top
9. A on "Now Playing" → **Music Player** resumes
10. All tracks end → resume cleared → back to **File Browser** → back path to **Main Menu** → no first item
