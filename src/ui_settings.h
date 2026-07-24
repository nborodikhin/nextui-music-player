#ifndef __UI_SETTINGS_H__
#define __UI_SETTINGS_H__

#include <stdbool.h>

#include <SDL2/SDL.h>

// Settings menu items — stable IDs. The visible list is built at render time
// (see settings_build_visible_items) so conditional rows can be inserted.
#define SETTINGS_ITEM_SCREEN_OFF        0
#define SETTINGS_ITEM_BASS_FILTER       1
#define SETTINGS_ITEM_SOFT_LIMITER      2
#define SETTINGS_ITEM_COLLECT_CRASH     3
#define SETTINGS_ITEM_DELETE_CRASH      4  // conditional — only when bundles exist
#define SETTINGS_ITEM_CLEAR_CACHE       5
#define SETTINGS_ITEM_UPDATE_YTDLP      6
#define SETTINGS_ITEM_ABOUT             7

// Maximum number of rows the settings list can have at once.
#define SETTINGS_VISIBLE_MAX            8

// Populate out[] with the IDs of currently visible items in display order.
// Returns the number written. Callers must size out for SETTINGS_VISIBLE_MAX.
// has_crash_bundles is passed in (not queried here) so the caller can cache the
// SD-card scan once per menu entry instead of hitting the filesystem per frame.
int settings_build_visible_items(int* out, int max_count, bool has_crash_bundles);

// Render the settings menu
// menu_selected: position in the visible list (0..count-1), not the item ID
// menu_scroll: index of the first visible row (top of the visible window)
// has_crash_bundles: cached predicate, threaded through so render and the input
//   loop build the identical list from one source of truth.
void render_settings_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                          int menu_scroll, bool has_crash_bundles);

// Render yt-dlp update progress screen
void render_ytdlp_updating(SDL_Surface* screen, int show_setting);

#endif
