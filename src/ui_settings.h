#ifndef __UI_SETTINGS_H__
#define __UI_SETTINGS_H__

#include <SDL2/SDL.h>

// Settings menu items - stable IDs, matching display order.
#define SETTINGS_ITEM_SCREEN_OFF    0
#define SETTINGS_ITEM_BASS_FILTER   1
#define SETTINGS_ITEM_SOFT_LIMITER  2
#define SETTINGS_ITEM_AUTO_UPDATE   3
#define SETTINGS_ITEM_CLEAR_CACHE   4
#define SETTINGS_ITEM_UPDATE_YTDLP  5
#define SETTINGS_ITEM_ABOUT         6
#define SETTINGS_ITEM_COUNT         7

// Render the settings menu.
// menu_selected: currently selected menu item
// menu_scroll: index of the first visible row. The rows do not all fit one page
//   at scale 3, so this list is windowed like any other.
void render_settings_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                          int menu_scroll);

// Render yt-dlp update progress screen
void render_ytdlp_updating(SDL_Surface* screen, int show_setting);

#endif
