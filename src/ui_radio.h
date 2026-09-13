#ifndef __UI_RADIO_H__
#define __UI_RADIO_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "radio.h"

// The lists and the setup screens of the radio. The playing screen is in
// ui_radio_playing.h.

// Render the radio station list
void render_radio_list(SDL_Surface* screen, int show_setting,
                       int radio_selected, int* radio_scroll);

// Render add stations - country selection screen
void render_radio_add(SDL_Surface* screen, int show_setting,
                      int add_country_selected, int* add_country_scroll);

// Render add stations - station selection screen
void render_radio_add_stations(SDL_Surface* screen, int show_setting,
                               const char* country_code,
                               int add_station_selected, int* add_station_scroll,
                               const int* sorted_indices, int sorted_count);

// Render help/instructions screen
void render_radio_help(SDL_Surface* screen, int show_setting, int* help_scroll);

// True while a title of a list of this module moves.
bool radio_list_needs_scroll_refresh(void);

// True while the scroll waits to start and needs one more frame.
bool radio_list_scroll_needs_render(void);

// Move the title without a redraw of the screen.
void radio_list_animate_scroll(void);

// Forget the title that moves. Call this on the way out of a list.
void radio_list_clear_scroll(void);

#endif
