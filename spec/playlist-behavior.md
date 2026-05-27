# Spec: Playlist Behavior (User-Facing)

## Two Kinds of "Playlist"

The app has two distinct playlist concepts that share the player but differ in lifetime and purpose:

| | Directory Queue | M3U Playlist |
|-|----------------|--------------|
| **Created by** | Automatically when user plays any file | User explicitly (Playlist Manager or "Add to Playlist") |
| **Lifetime** | In-memory, lost on app exit | Persisted as `.m3u` file on SD card |
| **Contents** | All audio files in a directory, in playback order | User-curated tracks from any location |
| **Editable during playback** | No | Yes (add from browser, remove from Playlist Manager) |
| **Max tracks** | 500 | 500 |

---

## Directory Queue

### How It Is Built

Whenever the user selects an audio file in the file browser (A button), the app builds an in-memory queue from the entire directory:

**Track order:**
1. The selected track (always index 0, plays first)
2. Files alphabetically after the selected track (same directory)
3. Files alphabetically before the selected track (same directory)
4. Audio files in subdirectories — depth-first, alphabetically sorted, max depth 10

Hidden files (starting with `.`) and symlinks are skipped. Non-audio files are ignored.

**"Play All" entry** (shown at the top of every folder): builds the queue starting from the first file alphabetically, same order as above but no specific start track.

**Limits**: maximum 500 tracks. Once full, additional files (including from subdirectories) are skipped silently.

### Playback Behavior

The directory queue behaves identically to an M3U playlist during playback — same controls, same shuffle/repeat logic, same resume. The difference is it cannot be saved or edited.

**Track-position display.** The "track N of M" indicator on the player screen shows the **queue** position, not the directory position. Because the selected track is rebuilt to index 0, picking any file from a directory always displays as `01 / M` on first play; subsequent next/prev moves walk the queue in `[selected, after…, before…]` order. The number reflects internal queue order, not the alphabetical position in the source folder. For an M3U playlist (where the queue order matches the file order), the display does match the file position.

---

## M3U Playlists

### Storage Location

`<SD card>/.userdata/shared/music-player/playlists/<name>.m3u`

Max 50 playlists. Playlist names may not contain characters invalid in filenames.

### Playlist Manager (Library → Playlists)

**Access**: Main Menu → Library → Playlists

The Playlist Manager has two views:

---

#### List View — All Playlists

Shows all saved playlists with their track count. Starts with cursor at index 0.

**Navigation**
- Up / Down: move cursor (wraps)
- Left / Right: page up / page down (clamps at ends, no wrap)

**Actions**

| Button | What happens |
|--------|-------------|
| A | Open the playlist (enter Detail View) |
| Y | Create a new playlist (opens keyboard for name) |
| X | Delete the selected playlist (confirmation required) |
| B | Back to Library menu |
| START short | Controls help |
| START long | Quit confirmation |

**Create playlist (Y)**
- Opens on-screen keyboard with prompt "Playlist name"
- On confirm with a non-empty name:
  - If name is new: creates empty `.m3u` file → toast "Playlist created"
  - If name already exists: nothing created → toast "Already exists"
- On cancel or empty input: nothing happens

**Delete playlist (X)**
- Confirmation dialog: "Delete Playlist?" + playlist name
  - A: deletes the `.m3u` file, refreshes list, clamps cursor to last valid index → toast "Playlist deleted"
  - B: cancels, returns to list

---

#### Detail View — Tracks in a Playlist

Shows all tracks in the selected playlist. Cursor starts at index 0 on entry.

Tracks are loaded from the `.m3u` file in the order they were added. Files that no longer exist on the SD card are silently skipped on load.

**Navigation**
- Up / Down: move cursor (wraps)
- Left / Right: page up / page down (clamps at ends, no wrap)

**Actions**

| Button | What happens |
|--------|-------------|
| A | Play the playlist starting from the selected track |
| X | Remove the selected track (confirmation required) |
| B | Back to List View (refreshes playlist track counts) |
| START short | Controls help |
| START long | Quit confirmation |

**Remove track (X)**
- Confirmation dialog: "Remove Track?" + track filename
  - A: rewrites the `.m3u` file without that track, refreshes detail view, clamps cursor to last valid index → toast "Track removed"
  - B: cancels

**Play (A)**
- Launches the music player at the selected track index
- All tracks in the playlist are available for playback (Up/Down, L1/R1 to navigate)
- On return from player: returns to Detail View, cursor clamped to last valid index

---

### Adding Tracks to a Playlist (from File Browser)

**Access**: File Browser (Library → Files) → navigate to any audio file or directory → press Y

When Y is pressed on an **audio file**, the file list is `[that file]` (1 file).

When Y is pressed on a **directory**, the app recursively collects all playable audio files in that directory and its subdirectories (same depth limit as directory queue: max depth 10, max 1000 files). Hidden files and symlinks are skipped. If no playable files are found, Y is a no-op.

Y is **inactive** on the following entries (press is a no-op):
- The parent-directory entry (`..`) shown at the top of any subfolder. Y must not add files from the parent — to add the parent's contents the user navigates up first.
- The synthetic `Play All` entry. It's a playback shortcut, not a navigable directory.

The "Add to Playlist" dialog overlays the current screen. It shows:

```
Add to Playlist: (N files)
──────────────────────────────
+ New Playlist
<Playlist A> (3)
<Playlist B> (12)
...
──────────────────────────────
A: Select   B: Cancel
```

Where N is the count of files being added ("1 file", "3 files", etc.).

- Up / Down to navigate (wraps)
- A to select, B to cancel
- Max 6 items visible at a time; scrolls with `...` indicators

Each file is stored in the `.m3u` with its full path as the track entry and the **filename without extension** as the `#EXTINF` display name (e.g. `"101 A Premonition"`, not the full path or the extension).

> **TODO:** Prefer the track name from file metadata (ID3 tag / Vorbis comment / etc.) as the `#EXTINF` display name; fall back to filename-without-extension only when metadata is missing. Tracked in code at `src/add_to_playlist.c:display_name_from_path`.

**Selecting "+ New Playlist":**
- Opens on-screen keyboard for playlist name
- On confirm with non-empty name:
  - Creates the playlist and immediately adds all collected files → toast "Added M/N files to <name>"
- On cancel: dialog closes with no action

**Selecting an existing playlist:**
- Files already in the playlist are silently skipped
- Appends new files → toast "Added M/N files to <name>" where M = files actually added, N = total files collected

(When M = 0, the toast reads "Added 0/N files to <name>", making the all-duplicates case explicit.)

The toast appears in the file browser view after the dialog closes. It persists for 3 seconds.

---

## Playback Controls (Both Playlist Types)

### Standard Controls

| Button | Action |
|--------|--------|
| A | Play / Pause |
| B (playing) | Background playback — audio continues, return to menu |
| B (paused) | Stop, return to file browser |
| Up / R1 | Next track |
| Down / L1 | Previous track |
| Left (hold) | Rewind 5 seconds |
| Right (hold) | Fast-forward 5 seconds |
| X | Toggle Shuffle (on/off) |
| Y | Toggle Repeat (on/off) |
| L2 / L3 | Cycle spectrum visualizer |
| R2 / R3 | Toggle lyrics |
| Select (tap) | Start screen-off countdown |
| Select + A | Wake screen |
| START short | Controls help |
| START long | Quit confirmation |

### Shuffle

- Toggled with X; state persists for the session but is **not** saved to disk
- When a track ends naturally with shuffle ON:
  - With a playlist (M3U or directory queue): picks a random track ≠ current from the playlist; distributes uniformly with no guaranteed no-repeat history
  - Without a playlist: picks a random audio file from the current browser folder ≠ current
- **Shuffle does not affect Up/Down / L1/R1** — those always navigate sequentially

### Repeat

- Toggled with Y; state persists for the session but is **not** saved to disk
- When a track ends with repeat ON: the same track restarts from the beginning
- **Repeat takes priority over shuffle** — if both are on, the current track repeats and shuffle is ignored

### Next / Previous Behaviour

**With a playlist (directory queue or M3U):**

| Situation | Result |
|-----------|--------|
| Next at last track | Wraps to first track |
| Prev at first track | Wraps to last track |
| Next in the middle | Advances one step |
| Prev in the middle | Goes back one step |

Manual navigation wraps at both ends. Natural end-of-playlist (track finishes playing) does **not** wrap — see End of Playlist below.

**Without a playlist** (rare fallback — file played directly without building queue):
- Next: next audio file in the browser directory (scans forward, no wrap)
- Prev: previous audio file in the browser directory (scans backward, no wrap)

### End of Playlist (Natural Playback)

When the last track finishes and neither repeat nor shuffle causes another track to start:
- Resume state is cleared
- If playing from the file browser: returns to browser view
- If playing from the Playlist Manager: returns to Playlist Detail View

---

## Resume State

Resume state is saved automatically and survives app restarts.

| Trigger | What is saved |
|---------|--------------|
| Track starts | Folder/playlist path, track path, track name, track index, position 0 |
| Every 5 seconds during playback | Current position (ms) |
| Track changes | New track path, name, index; position 0 |

Two resume types:

- **Files** (`RESUME_TYPE_FILES`): saves the folder path and rebuilds the directory queue on resume, starting from the saved track at the saved position
- **Playlist** (`RESUME_TYPE_PLAYLIST`): saves the `.m3u` path; on resume, reloads the M3U and seeks to the saved track + position

Resume state is shown in the main menu as "Resume: \<track name\>".

Resume is cleared when:
- The playlist reaches its natural end (all tracks played through)
- User presses X on the Resume item in the main menu

---

## Limits Reference

| Limit | Value |
|-------|-------|
| Max tracks in a directory queue | 500 (`PLAYLIST_MAX_TRACKS`) |
| Max directory scan depth | 10 (`PLAYLIST_MAX_DEPTH`) |
| Max files collected for "Add to Playlist" (directory Y) | 1000 |
| Max M3U playlists | 50 (`MAX_PLAYLISTS`) |
| Max tracks per M3U | 500 (`PLAYLIST_MAX_TRACKS`) |
| Max playlist name length | 128 characters |

---

## Testable Logic (Pure, No Platform Deps)

1. **Directory queue ordering**: given a sorted file list and a start index, the output order is `[selected, after..., before..., subdir files...]`
2. **`PlayerModule_nextTrack()` wraps**: at `current_index == track_count - 1`, plays index 0 (`Playlist_next` returns -1; caller wraps)
3. **`PlayerModule_prevTrack()` wraps**: at `current_index == 0`, plays index `track_count - 1` (`Playlist_prev` returns -1; caller wraps)
3a. **`Playlist_next()` no-wrap**: still returns -1 at end — `handle_track_ended()` relies on this for end-of-playlist detection
4. **`Playlist_shuffle()` avoids current**: result ≠ `current_index` when `track_count > 1`
5. **`Playlist_shuffle()` on single track**: returns 0
6. **`handle_track_ended()` priority**: repeat → replay same; shuffle → random; linear → Playlist_next
7. **End of linear playlist**: `Playlist_next()` returns -1 → `handle_track_ended()` returns false → `Resume_clear()` called
8. **Duplicate track guard**: `M3U_containsTrack()` must return true for an exact path already in the file → toast "Already in X", no append
9. **Cursor clamping after delete**: after playlist/track deletion, `selected = min(selected, count - 1)`, minimum 0
10. **AddToPlaylist item count**: `total_items = playlist_count + 1` (the +1 is "+ New Playlist")
11. **Resume type routing**: files playback → `RESUME_TYPE_FILES`, M3U playback → `RESUME_TYPE_PLAYLIST`
12. **Resume position update interval**: 5000ms (`now - last_resume_save > 5000`)
