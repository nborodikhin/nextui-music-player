# Adding an Audiobook Feature

## Context

The NextUI player currently handles three domains (Music, Radio, Podcast), but
none of them are suited to listening to audiobooks. The music module
(`module_player.c`) is designed for short tracks: its resume flow goes through
`resume.c`, which only keeps **a single** global position, its D-Pad shortcuts
do next/prev track, and it has no notion of a "book," a chapter, or "finished."

Goal: rename the "Library" menu entry to "Music," add an "Audiobook" entry
right below it, and make it the main entry point for managing and listening
to audiobooks, read from `SDCARD_PATH/Audiobook` (not `/Music`), with reliable
resume at the exact point where listening stopped — **one saved position per
book** — so the user can switch between several in-progress books.

### Decisions made

| Topic | Choice |
|---|---|
| Definition of a book | A subfolder of `/Audiobook` (sorted files = chapters) **or** a single `.m4b` file with internal chapters |
| Persistence | One progress entry per book, in a dedicated file |
| First-batch features | Chapter list · Sleep timer · Skip ±30s / ±10s |
| Background | New `BG_AUDIOBOOK` type |
| Out of scope | Playback speed (the engine already exists, see "Possible follow-ups") |

---

## What's missing today (gap analysis)

### 1. `.m4b` format — not supported at all
`Player_detectFormat()` (`src/player.c:2083-2096`) doesn't know the `.m4b`
extension. Yet a `.m4b` file is an MP4/AAC container identical to the already
supported `.m4a` (minimp4 + FDK-AAC, `src/player.c:425-490`): decoding will
work as soon as the extension is added. Notable detail: **`res/icon-m4b.png`
is already shipped in the pak but not referenced anywhere in the code** — it's
waiting for its `AudioFormat`.

### 2. Internal chapters — parsing absent
`src/audio/minimp4.h` parses neither the `chpl` atom (Nero chapters, the most
common case in `.m4b`) nor QuickTime chapter tracks (`tref`/`chap` — only the
`tref` box type is declared, `minimp4.h:533`, with no extraction). A small
standalone parser needs to be written.

### 3. Progress: single-slot model
`resume.c` only stores a single state (`ResumeState`, `src/resume.h:16-24`),
overwritten on every playback. There's no "library of in-progress books"
model, no notion of "finished." The podcast model (`progress.json`,
`src/podcast.c:1513-1565`) is the right pattern to follow, but has two flaws
not to reproduce: a fixed 500-entry array with no eviction
(`src/podcast.c:163`, further entries are silently lost) and a flush that
rewrites the whole JSON every 30s.

### 4. Book metadata — nonexistent
There's no notion of author/narrator/total duration/chapter count aggregated
at the folder level. The folder needs to be scanned, durations summed, and
**the result cached**: opening 40 MP3s to compute a total duration is too slow
on an SD card every time the list is displayed.

### 5. Unsuitable navigation
In `module_player.c`, D-Pad up/down = next/prev track and left/right =
continuous fast-forward/rewind. For a book, fixed jumps of ±10s / ±30s are
needed (the podcast module already does this, hardcoded:
`src/module_podcast.c:975-991`), along with a navigable chapter list.

### 6. Sleep timer — doesn't exist
There's no auto-stop timer anywhere in the code. Not to be confused with
screen-off (`Settings_getScreenOffTimeout()` /
`ModuleCommon_checkAutoScreenOffTimeout()`), which turns off the backlight
without stopping audio.

### 7. Hardcoded menu indices — blocking debt
Adding a 5th entry breaks several places that assume 4 items:
- `src/module_menu.h:8-12`: `MENU_LIBRARY 1` … `MENU_SETTINGS 4`
- `src/module_menu.c:53`: `int item_count = has_first ? 5 : 4;`
- `src/ui_main.c:17-18`: the two `menu_items_*` arrays
- `src/ui_main.c:56`: `int settings_index = has_first ? 4 : 3;` (update badge)
- `src/ui_main.c:388-490`: the contextual help screen `switch`

### 8. Miscellaneous
- `background.c` only knows 3 types; `Background_stopAll()`/`Background_tick()`
  are `switch` statements that need completing (`src/background.c:33-36`,
  `:69-71`).
- The podcast's `backgroundTick` fully duplicates the PLAYING loop
  (`src/module_podcast.c:1151-1196` vs `:1022-1058`) — **do not reproduce**
  this duplication for the audiobook.
- GPU layer number conflict: `LAYER_PLAYTIME`/`LAYER_PODCAST_PROGRESS` are
  both 3 (`src/ui_music.h:11`, `src/ui_podcast.h:76`). No risk (the modules
  are mutually exclusive), but layer 3 should be reused and cleaned up on exit.

---

## Implementation plan

### Step 0 — Rename "Library" → "Music" + new menu entry

Files: `src/module_menu.h`, `src/module_menu.c`, `src/ui_main.c`,
`src/module_library.c`.

- `module_menu.h`: insert `MENU_AUDIOBOOK 2` and shift `MENU_RADIO 3`,
  `MENU_PODCAST 4`, `MENU_SETTINGS 5`.
- `module_menu.c:53`: `item_count = has_first ? 6 : 5`.
- `ui_main.c:17-18`: `{"Resume", "Music", "Audiobook", "Online Radio", "Podcasts", "Settings"}`
  and the variant without the first item.
- `ui_main.c:56`: replace the hardcoded `4 : 3` with a value derived from
  `MENU_SETTINGS` — this is the most likely bug in this step.
- `module_library.c`: submenu title `"Library"` → `"Music"` (line 31).
  **Keep the `LibraryModule_*` file/function names** — a full rename would
  add noise with no value.
- `musicplayer.c:155-180`: add the `case MENU_AUDIOBOOK`.

Standalone, testable step: at this point "Audiobook" is displayed and opens an
empty screen.

### Step 1 — Data layer: `src/audiobook.c` / `audiobook.h`

New data module, modeled on `podcast.c` but without networking.

```c
typedef struct {
    char title[256];        // folder name, or .m4b album tag
    char author[128];       // artist tag if available
    char path[512];         // folder, or path to the .m4b
    bool single_file;       // true = .m4b with internal chapters
    int  chapter_count;
    int  total_duration_ms;
    // progress
    int  current_chapter;
    int  position_ms;       // position WITHIN the current chapter
    bool finished;
    uint32_t last_played;   // for sorting "in progress" to the top
} Audiobook;

typedef struct {
    char title[256];
    char path[512];         // == book.path if single_file
    int  start_ms;          // offset within the file (0 if multi-file)
    int  duration_ms;
} AudiobookChapter;
```

API: `Audiobook_init/cleanup`, `Audiobook_scanLibrary`, `Audiobook_getCount/get`,
`Audiobook_loadChapters(book)`, `Audiobook_saveProgress(book, chapter, pos_ms)`,
`Audiobook_markFinished`, `Audiobook_flushProgress`.

Persisted to `SHARED_USERDATA_PATH/music-player/audiobooks.json` via **parson**
(already linked, `src/include/parson`), keyed by the book's relative path. Use
a `JSON_Object` (map) rather than a linearly-scanned array, and a **linked
list / dynamic array** — no fixed cap like `MAX_PROGRESS_ENTRIES`.

`finished` as a **distinct boolean**, not the podcast's `-1` sentinel: for a
book we want to be able to be "finished" *and* keep the position to re-listen
to the ending.

Separate scan cache (`library.json`: durations and chapter counts per book) to
avoid reopening every file; invalidated by the folder's `mtime`.

Reuse: `Browser_isAudioFile()` and the sort from `browser.c:36`,
`Player_detectFormat()`.

### Step 2 — `.m4b` support and chapter extraction

- `src/player.c:2083-2096`: add `AUDIO_FORMAT_M4B` (new value in
  `src/player.h:9-19`) mapped to the same decode path as `AUDIO_FORMAT_M4A`
  (`src/player.c:425+`, remember the `switch` statements in
  `stream_decoder_init` and `Player_stop`).
- `src/ui_icons.c`: wire up `res/icon-m4b.png` in `Icons_getForFormat()`.
- New `src/m4b_chapters.c` / `.h`: standalone parser that reopens the file and
  looks for `moov` → `udta` → `chpl` (version/flags, count, then for each
  entry a 64-bit timestamp in 100ns units + a UTF-8 Pascal string title).
  Fallback to the QuickTime chapter track (`trak` referenced by `tref`/`chap`,
  titles in `text` samples) only if `chpl` is absent — otherwise a single
  chapter covering the whole file.
- Seeking to a chapter of a `.m4b` is a simple `Player_seek(start_ms)`: the
  existing streaming seek (`src/player.c:2334-2352`) already handles it.

### Step 3 — UI module: `src/module_audiobook.c` + `src/ui_audiobook.c`

Structure modeled on `module_podcast.c` (state machine + `GFX_startFrame` /
`PAD_poll` / `ModuleCommon_handleGlobalInput` loop):

```
AUDIOBOOK_STATE_LIBRARY    // book list, "in progress" at the top
AUDIOBOOK_STATE_CHAPTERS   // book's chapter list
AUDIOBOOK_STATE_SEEKING    // async seek in progress
AUDIOBOOK_STATE_PLAYING
```

Reuse `ui_utils.h` extensively: `render_screen_header`, `render_list_item_text`,
`render_scroll_indicators`, `render_empty_state`, `render_toast`, `format_time`,
`calc_list_layout` / `list_page_up` / `list_page_down`.

**Async resume**: copy the `Podcast_loadAndSeek()` pattern
(`src/podcast.c:1434-1468`) + a SEEKING state that polls `Player_resume()`
(`src/module_podcast.c:880-899`) — note that `Player_resume()` returns
*"a seek is in progress,"* the name is misleading.

**Controls** (PLAYING state):
| Button | Action |
|---|---|
| A | Play / Pause |
| B | Back (audio keeps playing in background) |
| D-Pad ←/→ | −10s / +30s (`PAD_justRepeated`, like `module_podcast.c:975-991`) |
| D-Pad ↑/↓ | Previous / next chapter |
| L1/R1 | Previous / next chapter |
| X | Chapter list |
| Y | Sleep timer (cycle: Off → 15 → 30 → 45 → 60 min → end of chapter) |
| Select | Turn off screen |

GPU progress bar on `LAYER_PODCAST_PROGRESS` (=3), copying `PodcastProgress_*`
(`src/ui_podcast.c:1895-1972`) — redrawn only when the second changes. Show
**remaining time in the chapter + overall book progress** (the podcast module
only shows elapsed/total; this is an addition).

Add a contextual help state (free constants after `LIBRARY_MENU_HELP_STATE 55`:
60/61/62) and the corresponding `case` statements in `ui_main.c:388-490`.

### Step 4 — Background and save integration

- `src/background.h:11`: add `BG_AUDIOBOOK`.
- `src/background.c`: the `switch` statements in `Background_stopAll()` and
  `Background_tick()`.
- `src/ui_main.c:26-33`: `get_now_playing_label()` → `case BG_AUDIOBOOK: return "Audiobook";`
- `src/musicplayer.c:143-152`: route "Now Playing" to `AudiobookModule_run`.
- **Factor out** the tick logic (periodic save + chapter advance) into a
  single static function called by both the PLAYING loop *and*
  `AudiobookModule_backgroundTick()`.

Save points: every 30s during playback, on pause, on chapter change, on B, on
`Background_stopAll()`, at the end of the book (`finished = true`), and in
`musicplayer.c`'s cleanup.

### Step 5 — Sleep timer

Implement in `module_audiobook.c` (local state: `sleep_deadline_ms`,
`sleep_at_chapter_end`), evaluated **inside the factored-out tick function** so
it also works while the book plays in the background. On expiry:
`Player_pause()`, save progress, `Background_setActive(BG_NONE)`, re-enable
autosleep via `ModuleCommon_setAutosleepDisabled(false)`. Show the remaining
time on the playback screen when the timer is armed.

### Step 6 — Build and packaging

- `src/Makefile`, `LOCAL_SRC` (~line 126): add `audiobook.c m4b_chapters.c
  module_audiobook.c ui_audiobook.c`.
- `module_audiobook.c`: `mkdir(AUDIOBOOK_PATH, 0755)` at init, like
  `module_player.c:87`, so the folder appears on the SD card.
- `README.md`: Features + Controls section.
- `pak.json`: version bump **only via `python3 update_version.py`** (it syncs
  `state/app_version.txt`, `src/selfupdate.h`, `src/qr_code_data.h`).

---

## Verification

### Prerequisites — to finish BEFORE writing any code

State verified on this machine:

**✅ Done**
- NextUI checkout present (`~/workspace/NextUI`, with `workspace/desktop/`).
- Symlink in place:
  `~/workspace/NextUI/workspace/nextui-music-player → ~/workspace/nextui-music-player`.
  Required: the `Makefile` references `../../all/common`,
  `../../desktop/platform`, `../../desktop/libmsettings`, and
  `build-desktop.sh` computes `NEXTUI_ROOT="$HERE/../.."`. **Always build from
  the linked path**, never from `~/workspace/nextui-music-player`.
- Homebrew packages: `gcc` (16.1.0), `make`, `sdl2_image`, `sdl2_ttf`,
  `sdl2-compat`, `libsamplerate`, `libzip`, `sqlite`, `pkgconf`.

**⚠️ Remaining — two manual fixes**

1. **Create the `ar` symlink by hand.** `macos_create_gcc_symlinks.sh`
   generates `/usr/local/bin/<tool>` links by stripping the version suffix,
   but the special case that creates `ar` hardcodes `gcc-ar-14` (line ~118),
   whereas the `gcc` installed here is **16**. The `ar` link therefore won't
   be created, and `workspace/all/minarch/makefile:90` (`AR=$(CROSS_COMPILE)ar`,
   i.e. `/usr/local/bin/ar`) is used by `make build PLATFORM=desktop`:
   ```sh
   sudo ln -s /opt/homebrew/bin/gcc-ar-16 /usr/local/bin/ar
   ```
   Do this **after** the first `build-desktop.sh` run (the script does
   `rm -f /usr/local/bin/*` at the start, which would erase a link created
   beforehand).
2. **Execution order.** `build-desktop.sh` runs
   `sudo macos_create_gcc_symlinks.sh` on its own on the first run and will
   ask for the password. That script does `rm -f "/usr/local/bin"/*`: harmless
   here (the directory doesn't exist yet), but worth knowing before rerunning
   it someday on a machine where `/usr/local/bin` contains other things.

`/var/tmp/nextui` (fake SD root + `libmsettings.so`) is created automatically
on the first run by `prepare_fake_sd_root.sh`.

**Startup sequence**
```sh
cd ~/workspace/NextUI/workspace/nextui-music-player
sh build-desktop.sh --build          # setup + gcc symlinks + build NextUI + build the pak
sudo ln -s /opt/homebrew/bin/gcc-ar-16 /usr/local/bin/ar   # if the build failed on `ar`
sh build-desktop.sh                  # build + launch
```
If `make build PLATFORM=desktop` fails partway through, the marker
`/var/tmp/nextui/.desktop_setup_ok` isn't written and the whole setup replays
on the next run — this is intentional (`build-desktop.sh:38-63`).

Note: Homebrew replaced `sdl2` with **`sdl2-compat`** (SDL2 API on top of
SDL3). It does provide `libSDL2`, but it's the first candidate to check if
odd graphics or audio behavior shows up at launch.

### Limitation of the desktop target: no AAC

`src/fdk_aac_desktop_stub.c` stubs out all AAC decoding on desktop. So:
- **testable on desktop**: all of the Audiobook UI, navigation, progress
  persistence, sleep timer, chapter list, and `chpl` parsing (it doesn't touch
  FDK) — using MP3/FLAC/OGG books;
- **device required**: actual `.m4b` audio playback.

Option to shorten the dev loop, to be decided: `brew install fdk-aac` and
actually link the lib on Darwin instead of the stub (the package exists on
macOS; its absence on Debian is what motivated the stub). This would be a
Makefile change outside the initial scope.

### Test set

Dev loop once setup has passed:
```sh
cd ~/workspace/NextUI/workspace/nextui-music-player
sh build-desktop.sh          # incremental rebuild + launch
sh build-desktop.sh --build  # build only
```

Test files go into the fake SD root used by this target:
`/var/tmp/nextui/sdcard/Audiobook/`. Prepare:
- a `TestBook/` folder with 3 short MP3s named `01…03`
- a real chaptered `.m4b` (generate one: `ffmpeg -i in.mp3 -f ffmetadata` +
  `-map_metadata` with `[CHAPTER]` entries, output `.m4b`)
- a single-file book with no chapters (degraded case)

Persisted progress to check in
`/var/tmp/nextui/sdcard/.userdata/shared/music-player/audiobooks.json`.

Scenarios to validate:
1. Main menu: "Music" then "Audiobook" in the right order; the "Update
   available" badge stays on Settings (the most likely index regression).
2. Empty `/Audiobook` → empty-state screen, no crash.
3. Play a multi-file book → exit via B → "Now Playing: Audiobook" in the menu
   → return to it: exact resume.
4. Book A → book B → back to book A: each book kept **its own** position.
5. Quit the app mid-playback, relaunch: position restored from the JSON.
6. `.m4b`: correct chapter list, direct jump to a chapter, resume at the
   right offset.
7. ±10/±30s skips at the start and end of a chapter (bounds, no negative
   position).
8. Sleep timer, 1 minute (temporary test value): stops in foreground **and**
   in background.
9. End of the last chapter → book marked finished, `BG_AUDIOBOOK` released.
10. No regressions: Music / Radio / Podcast unchanged; starting a book does
    stop music playback and vice versa (mutual exclusion via
    `Background_stopAll()`).

Device build before shipping:
```sh
sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5040'
PLATFORM=tg5050 sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5050'
```
⚠️ The `desktop` target **has no AAC decoder** (`fdk_aac_desktop_stub.c`):
`.m4b` files won't decode natively. Test chapter parsing on desktop (it
doesn't use FDK), but `.m4b` playback must be tested on device.

---

## Possible follow-ups (out of scope)

- **Playback speed**: `Player_setPlaybackSpeed()` already exists and is
  generic (`src/player.c:2370`). Two caveats: the resampling doesn't preserve
  pitch (`src/player.c:1085-1100`) — audible on voice — and `Player_stop()`
  resets speed to 1.0 (`src/player.c:2283`), so it would need to be persisted
  per book.
- Cover art import (folder's `cover.jpg` or the `.m4b`'s embedded artwork).
- Named bookmarks.
