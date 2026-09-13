#ifndef __UI_MUSIC_H__
#define __UI_MUSIC_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "browser.h"
#include "player.h"

// The file browser of the music player. The playing screen is in
// ui_music_playing.h.

// Render the file browser screen
void render_browser(SDL_Surface* screen, int show_setting,
                    BrowserContext* browser);

// Check if browser list has active scrolling (for refresh optimization)
bool browser_needs_scroll_refresh(void);

// Check if browser scroll needs a render to transition (delay phase)
bool browser_scroll_needs_render(void);

// Animate browser scroll only (GPU mode, no screen redraw needed)
void browser_animate_scroll(void);

#endif
