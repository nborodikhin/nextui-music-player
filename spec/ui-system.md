# Spec: UI System

## Rendering Stack

The app uses a hybrid SDL2 + OpenGL ES 2.0 rendering model inherited from the NextUI platform:

```
GFX_init(MODE_MAIN)      → SDL2 surface (software framebuffer)
GFX_clear(screen)        → clear surface
SDL_BlitSurface(...)     → blit textures/text onto surface
GFX_flip(screen)         → present to display
```

GPU layers (GLES) are used for the spectrum visualizer only (`LAYER_SPECTRUM = 5`). All other UI is CPU-rendered SDL2 surfaces.

## Coordinate System & Scaling

The NextUI platform provides `SCALE1(n)` / `SCALE2(n)` macros (from `api.h`) that convert logical pixel sizes to physical pixels based on device DPI. Always use `SCALE1()` for UI dimensions so layouts are correct on both tg5040 and tg5050 resolutions.

## Font System (`ui_fonts.{c,h}`)

Three fonts loaded from `res/font.ttf`:

| Function | Usage |
|----------|-------|
| `Fonts_getTitle()` | Large text (track title, menu headers) |
| `Fonts_getLarge()` | Medium-large text |
| `Fonts_getSmall()` | Small text (metadata, status, toasts) |

Rendered via `TTF_RenderUTF8_Blended()`. Caller frees the returned surface.

## Icon System (`ui_icons.{c,h}`)

PNG icons for file type indicators in the browser:

| Icon | File |
|------|------|
| `icon-mp3.png` | MP3 files |
| `icon-flac.png` | FLAC files |
| `icon-wav.png` | WAV files |
| `icon-ogg.png` | OGG files |
| `icon-m4a.png` | M4A files |
| `icon-aac.png` | AAC files |
| `icon-ops.png` | Opus files |
| `icon-m3u.png` | M3U playlists |
| `icon-m4b.png` | M4B audiobooks |
| `icon-folder.png` | Directories |
| `icon-play-all.png` | "Play All" action |
| `icon-download.png` | Download action |
| `icon-complete.png` | Downloaded state |
| `icon-audio.png` | Generic audio |
| `icon-empty.png` | Empty state |

`Icons_init()` loads all PNGs. `Icons_quit()` frees them. Access via `Icons_get(ICON_*)`.

## Color Palette

From `api.h` (NextUI platform defines):

| Constant | Usage |
|----------|-------|
| `COLOR_WHITE` | Primary text |
| `COLOR_GRAY` | Secondary text, hints |
| `COLOR_BLACK` | Backgrounds |
| `COLOR_SELECTED` | Selected list item highlight |

Do not hardcode SDL `SDL_Color` or hex values — always use these constants.

## List/Browser Widget (`browser.{c,h}`, `ui_utils.{c,h}`)

`browser.{c,h}` provides a generic scrollable file list widget:
- Keyboard navigation (D-Pad up/down)
- Page-up/down (L1/R1)
- Configurable item height, visible row count

`ui_utils.{c,h}` provides:
- **Scrolling text** (`scrolling_text_*`): for track titles that exceed the display width. Software-scrolls by pixel offset; `ui_main.c:menu_needs_scroll_redraw()` reports if continuous redraws are needed.
- List row rendering helpers

## Dirty Rendering

All modules use a `dirty` flag pattern — only re-render when state changes. This reduces CPU usage (important on embedded hardware).

```c
int dirty = 1;  // start dirty to draw first frame
while (1) {
    // input handling may set dirty = 1
    if (dirty) {
        GFX_clear(screen);
        render_module_ui(screen, ...);
        GFX_flip(screen);
        dirty = 0;
    }
    SDL_Delay(16);  // ~60fps cap
}
```

Exception: scrolling text requires continuous redraws while text is scrolling, even without input. Check `menu_needs_scroll_redraw()` for the main menu case.

## Overlay Dialogs

Two standard overlays rendered on top of any module:

1. **Controls help dialog** (`render_controls_help(screen, app_state)`)
   - Triggered by START short-press
   - Shows context-sensitive button mapping for `app_state`

2. **Quit confirmation dialog** (`render_confirmation_dialog(screen, content, title)`)
   - Triggered by START long-press
   - "A: Yes  B: No" standard prompt

3. **Screen-off hint** (`render_screen_off_hint(screen)`)
   - Shown for `SCREEN_OFF_HINT_DURATION_MS` (4s) before screen turns off

4. **Volume overlay**
   - Managed by NextUI `PWR_update()` / `ModuleCommon_PWR_update()`
   - Shown when hardware volume buttons pressed

## Toast Messages

Short-lived status messages at the bottom of the screen. Lifecycle:

```c
char toast_message[256];
uint32_t toast_time;
// Set:
snprintf(toast_message, sizeof(toast_message), "Added to playlist");
toast_time = SDL_GetTicks();
// Tick (in render loop):
ModuleCommon_tickToast(toast_message, toast_time, &dirty);
// Duration: TOAST_DURATION = 3000ms
```

## Album Art Display (`ui_album_art.{c,h}`)

Draws the `SDL_Surface* album_art` scaled to fit a target rect, maintaining aspect ratio. Falls back to a placeholder if NULL.

## TG5050 Display Recovery

Before launching external binaries (on-screen keyboard, yt-dlp for some operations):

```c
DisplayHelper_prepareForExternal();  // release DRM master
// ... launch external process ...
DisplayHelper_recoverDisplay();       // reacquire DRM, reinit SDL

// After returning to main loop:
SDL_Surface* ns = DisplayHelper_getReinitScreen();
if (ns) screen = ns;  // MUST update screen pointer
```

This pattern must be followed any time an external binary that uses the display is launched on tg5050.
