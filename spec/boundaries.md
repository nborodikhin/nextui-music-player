# Spec: Code Style & Boundaries

## Code Style

**Language**: C (gnu99). No C++.

**Naming conventions**:
- Functions: `ModuleName_verbNoun()` — e.g., `Player_getPosition()`, `Resume_saveFiles()`
- Types/structs: `PascalCase` — e.g., `PlayerContext`, `RadioStation`
- Enums: `UPPER_CASE` values — e.g., `PLAYER_STATE_PLAYING`, `MODULE_EXIT_QUIT`
- Constants/macros: `UPPER_CASE` — e.g., `PLAYLIST_MAX_TRACKS`, `TOAST_DURATION`
- Local variables: `snake_case`
- Header guards: `__FILENAME_H__`

**Module header pattern**:
```c
#ifndef __MODULE_PLAYER_H__
#define __MODULE_PLAYER_H__

#include <SDL2/SDL.h>
#include "module_common.h"

ModuleExitReason PlayerModule_run(SDL_Surface* screen);

#endif
```

**No inline function implementations in headers.** Headers are declarations only.

**Error returns**: functions return `0` on success, `-1` on failure. Never use exceptions or `assert()` in production paths.

**Memory**: no garbage collection. Every `malloc` has a matching `free`. `Playlist_init()` + `Playlist_free()` pattern for structs with heap members.

**Comments**: only when the WHY is non-obvious (hidden constraint, workaround, invariant). No docstrings. No "what this does" comments.

**No global mutable state** except in the owning `.c` file of each module. Globals are `static` in their `.c` file and accessed only through the module's public API.

## Example Snippet

```c
// Dirty rendering pattern — only redraw on state change
int dirty = 1;
while (1) {
    GlobalInputResult g = ModuleCommon_handleGlobalInput(screen, &show_setting, APP_STATE_PLAYER);
    if (g.should_quit) return MODULE_EXIT_QUIT;
    if (g.input_consumed) { dirty = 1; continue; }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return MODULE_EXIT_QUIT;
    }

    int btn = PAD_getPressed();
    if (btn & BTN_A) {
        Player_togglePause();
        dirty = 1;
    }

    if (dirty) {
        GFX_clear(screen);
        render_player_ui(screen, &state);
        GFX_flip(screen);
        dirty = 0;
    }
    SDL_Delay(16);
}
```

## Boundaries

### Always do
- Use `SCALE1()` / `SCALE2()` for all pixel dimensions (device-agnostic layout)
- Use `COLOR_WHITE` / `COLOR_GRAY` / etc. constants (never hardcode SDL_Color)
- Call `ModuleCommon_handleGlobalInput()` at the top of every module's input loop
- Follow `dirty` flag rendering — only call `GFX_flip()` when content changed
- Protect shared data with the module's `pthread_mutex_t`
- Update `Resume_saveFiles()` / `Resume_savePlaylist()` on track change and periodically during playback
- Follow `DisplayHelper_prepare/recover` pattern when launching external binaries on TG5050
- Build and test for both `tg5040` and `tg5050` before shipping

### Ask first
- Adding a new third-party library (affects both Docker toolchain images)
- Changing the SD card data layout (breaks existing user data)
- Modifying the `launch.sh` pak entry point (NextUI pak contract)
- Adding new network endpoints or changing existing API URLs
- Changing `pak.json` format fields
- Modifying the GitHub Actions release workflow

### Never do
- Commit secrets, API keys, or device-specific paths into source
- Block the main thread with network I/O or long file operations — use background threads
- Render outside the `dirty=1` check (wastes CPU on embedded hardware)
- Access another module's internal `static` state directly (use its public API)
- Skip `DisplayHelper_prepareForExternal()` before launching keyboard or other display-using processes on TG5050
- Use `assert()` in code paths that can be triggered at runtime
- Add C++ (`//` comments are fine in C99+, but no `.cpp` files, no classes)
- Remove or bypass the `ModuleCommon_handleGlobalInput()` call (breaks quit, volume, screen-off for that module)
