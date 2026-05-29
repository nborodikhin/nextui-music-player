#ifndef __UI_SETTINGS_H__
#define __UI_SETTINGS_H__

#include <SDL2/SDL.h>

// Render the settings menu
// menu_selected: currently selected menu item
// menu_scroll: index of the first visible item (top of the visible window)
void render_settings_menu(SDL_Surface* screen, int show_setting, int menu_selected, int menu_scroll);

// Render yt-dlp update progress screen
void render_ytdlp_updating(SDL_Surface* screen, int show_setting);

#endif
