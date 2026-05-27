# Spec: Menu Behavior (User-Facing)

## Menu States

The main menu has three variants determined by runtime conditions, evaluated on every frame:

### State A — No First Item (`first_item_mode = MENU_FIRST_NONE`)

**Condition**: No background audio playing AND no resume state saved.

**Visible items** (4 total, indices 0–3):
```
[0] Library
[1] Online Radio
[2] Podcasts
[3] Settings
```

Footer hint: `B: EXIT`

---

### State B — Resume (`first_item_mode = MENU_FIRST_RESUME`)

**Condition**: Resume state saved AND no background audio playing.

**Visible items** (5 total, indices 0–4):
```
[0] Resume: <track name>     ← scrolls if name overflows pill width
[1] Library
[2] Online Radio
[3] Podcasts
[4] Settings
```

Footer hint: `B: EXIT`

The track name portion of item 0 soft-scrolls horizontally when selected and the text is wider than the available space. The "Resume: " prefix is fixed.

---

### State C — Now Playing (`first_item_mode = MENU_FIRST_NOW_PLAYING`)

**Condition**: Background audio is playing (takes priority over Resume even if both are true).

**Visible items** (5 total, indices 0–4):
```
[0] Now Playing: Music       ← label depends on active background type
    Now Playing: Radio
    Now Playing: Podcast
[1] Library
[2] Online Radio
[3] Podcasts
[4] Settings
```

Footer hint: `B: EXIT`

The `Now Playing: <type>` label does **not** scroll — the type string ("Music", "Radio", "Podcast") is short and fixed.

---

### Settings Badge

When `SelfUpdate_getStatus()->update_available` is true, the Settings item displays as:
```
Settings (Update available)
```
This applies to the Settings item in all three menu states (index 3 in State A, index 4 in States B and C).

---

## Initial Cursor Position

The cursor always starts at index 0 when the menu is entered (including re-entry from a sub-module).

---

## Navigation

### D-Pad Up
- Moves cursor to `index - 1`
- At index 0: wraps to last index (`item_count - 1`)

### D-Pad Down
- Moves cursor to `index + 1`
- At last index: wraps to index 0

### D-Pad Left (page up)
- Moves cursor toward 0 by one page (`items_per_page`)
- Clamps at 0 (no wrap)

### D-Pad Right (page down)
- Moves cursor toward last index by one page (`items_per_page`)
- Clamps at `item_count - 1` (no wrap)

All navigation clears the GPU scroll-text layer (`GFX_clearLayers(LAYER_SCROLLTEXT)`) and marks the screen dirty.

---

## Button Actions

### A — Confirm

Selects the item at the current cursor index and returns to `main()`.

Return value mapping:

| Menu State | Cursor Index | Return Value | Constant |
|------------|-------------|--------------|----------|
| A (no first) | 0 | 1 | `MENU_LIBRARY` |
| A (no first) | 1 | 2 | `MENU_RADIO` |
| A (no first) | 2 | 3 | `MENU_PODCAST` |
| A (no first) | 3 | 4 | `MENU_SETTINGS` |
| B or C (has first) | 0 | 0 | `MENU_RESUME` / `MENU_NOW_PLAYING` |
| B or C (has first) | 1 | 1 | `MENU_LIBRARY` |
| B or C (has first) | 2 | 2 | `MENU_RADIO` |
| B or C (has first) | 3 | 3 | `MENU_PODCAST` |
| B or C (has first) | 4 | 4 | `MENU_SETTINGS` |

The offset formula: `return_value = cursor_index + (has_first ? 0 : 1)`

### B — Two-step exit

Exit on the Main Menu is a two-step gesture, to prevent accidental exits (B is the universal "back" elsewhere in the app; muscle-memory presses on the Main Menu would otherwise kill the app + background audio).

State: the menu tracks `exit_armed_at` (timestamp of the first B-press, or 0 when not armed). The window equals `TOAST_DURATION` (3000 ms), so the prompt toast and the arm-window expire together.

| State | B-press | Result |
|-------|---------|--------|
| Not armed (`exit_armed_at == 0`) | B | Arm: set `exit_armed_at = now`, show toast `"Press B again to exit"`. Cursor and menu state unchanged. |
| Armed, `now - exit_armed_at < TOAST_DURATION` | B | Returns `MENU_QUIT` (-1). |
| Armed, `now - exit_armed_at ≥ TOAST_DURATION` | B | Toast already auto-cleared; treated as a first press → re-arm. |

While armed, **other buttons behave normally** — Up/Down navigates, A opens an item (the menu loop exits cleanly via the selection return value), X clears history/playback, etc. The toast remains visible until its own `TOAST_DURATION` timeout; it does **not** track the user's other actions.

If a sub-module previously set a menu toast via `MenuModule_setToast(...)` and it's still visible, pressing B replaces it with the exit toast.

Background audio is unaffected by either step — it's only stopped (along with everything else) when the app actually exits via `MENU_QUIT`.

### X — Context Action on First Item

Only acts when cursor is at index 0 **and** a first item exists.

| Menu State | X Behavior |
|-----------|------------|
| State A (no first) | No effect |
| State B (Resume) | Calls `Resume_clear()`. Cursor stays at 0. Screen marks dirty. Menu transitions to State A on next frame. |
| State C (Now Playing) | Calls `Background_stopAll()`. Cursor stays at 0. Screen marks dirty. Menu transitions to State A or B on next frame depending on resume availability. |

X has **no effect** when cursor is at any index other than 0.

### START (short press — released in < 500ms)

Opens the **Controls Help** dialog (see below). Input is consumed from the moment START is pressed until the dialog is dismissed.

### START (long press — held ≥ 500ms)

Opens the **Quit Confirmation** dialog (see below). Input is consumed while held.

While waiting to determine short vs long press (START held but not yet at 500ms threshold), all input is consumed.

---

## Overlay Dialogs

Overlays are managed by `ModuleCommon_handleGlobalInput()` and take full control of input when active. They render on top of the current menu state without changing menu cursor position.

### Controls Help Dialog

Triggered by: START short press.

Dismissal: any button press closes it.

Contents displayed for `app_state = 0` (main menu):

| Button | Action |
|--------|--------|
| Up/Down | Navigate |
| Left/Right | Navigate |
| X | Clear History/Playback |
| Start (hold) | Exit App |

Footer note: "Press any button to close"

### Quit Confirmation Dialog

Triggered by: START long press (≥ 500ms).

Title: `"Quit Music Player?"`

| Button | Outcome |
|--------|---------|
| A | Returns `MENU_QUIT`, app exits |
| B | Dismisses dialog, returns to menu |
| START | Dismisses dialog, returns to menu |
| Any other button | Consumed, dialog stays open |

---

## Toast Messages

Any sub-module can call `MenuModule_setToast(msg)` before returning to menu. The toast appears at the bottom of the screen for 3000ms (`TOAST_DURATION`), then disappears. The menu marks dirty continuously while a toast is active.

Toast state is independent of menu cursor position and first_item_mode.

---

## Background Audio Effect on Menu

Every menu frame calls `Background_tick()`. This handles:
- Advancing to the next track in background music playback
- Saving resume position periodically

When `Background_isPlaying()` is true:
- `ModuleCommon_setAutosleepDisabled(true)` is called — screen auto-sleep is suppressed
- Menu is in State C (Now Playing first item)

The background state is re-evaluated every frame, so transitions (e.g. background audio ends naturally) take effect on the very next frame.

---

## Dynamic State Transitions Mid-Session

The menu continuously re-evaluates `first_item_mode` and `item_count` on every frame. Possible transitions while user is in the menu:

| Trigger | Transition |
|---------|-----------|
| Background audio ends naturally | State C → State B (if resume exists) or State A |
| X pressed on Now Playing | State C → State B or A |
| X pressed on Resume | State B → State A |
| Sub-module sets resume before returning | State A → State B on re-entry |

> **Edge case**: If menu cursor is at index 4 and the menu transitions from State B/C (5 items) to State A (4 items), the cursor index 4 is now out of bounds. The code does not clamp the cursor on transition — this should be treated as a bug and covered by a test.

---

## Screen Redraw Rules

The menu follows a `dirty` flag pattern:

| Trigger | dirty = 1? |
|---------|-----------|
| Any navigation input | Yes |
| A/B/X button | Implicit (function returns, no redraw needed) |
| Global input consumed | Conditional (`global.dirty`) |
| Toast active | Yes (continuous until toast expires) |
| Resume item is scrolling | Yes (continuous via `menu_needs_scroll_redraw()`) |
| No input, no toast, no scroll | No (GFX_sync() called instead) |

---

## Controls Help State Reference

The full `app_state` → screen binding table is in [`spec/ui-screens.md` — Controls Help State Reference](ui-screens.md#controls-help-state-reference).

---

## Testable Logic (Pure, No Platform Deps)

These behaviors are pure selection arithmetic and can be tested without SDL/PAD/GFX:

1. **Return value mapping**: given `(has_first, cursor_index)` → expected `MENU_*` constant
2. **Wrap-around navigation**: up at 0 → `item_count - 1`; down at last → 0
3. **Page navigation clamping**: left/right do not wrap, clamp at bounds
4. **item_count derivation**: 4 when no first item, 5 when first item present
5. **first_item_mode priority**: `Background_isPlaying()` → `NOW_PLAYING` overrides `Resume_isAvailable()` → `RESUME`
6. **Settings badge index**: index 3 in State A, index 4 in States B/C
7. **X button guard**: only fires when `menu_selected == 0` AND `first_item_mode != MENU_FIRST_NONE`
8. **Toast duration**: expires after `TOAST_DURATION` ms
9. **START long-press threshold**: `START_LONG_PRESS_MS = 500`
10. **Overlay release hide**: `OVERLAY_VISIBLE_AFTER_RELEASE_MS = 800`, `OVERLAY_FORCE_HIDE_DURATION_MS = 500`

## Behaviors That Require Platform Stubs

| Behavior | Requires |
|----------|---------|
| Navigation input | `PAD_justRepeated`, `PAD_justPressed` |
| Rendering output | `GFX_clear`, `GFX_flip`, SDL surface |
| Auto-sleep toggling | `PWR_disableAutosleep`, `PWR_enableAutosleep` |
| Screen-off hint | `PLAT_enableBacklight`, `SDL_GetTicks` |
| Volume sync | `GetVolume`, `SetVolume`, `Player_setVolume` |
| USB HID events | `Player_pollUSBHID` |
| Scroll-text redraw | `ScrollText_isScrolling`, SDL surface |
