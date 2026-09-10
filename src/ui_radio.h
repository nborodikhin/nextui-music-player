#ifndef __UI_RADIO_H__
#define __UI_RADIO_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "radio.h"

// GPU layer for buffer indicator
#define LAYER_BUFFER 4

// Render the radio station list
void render_radio_list(SDL_Surface* screen, int show_setting,
                       int radio_selected, int* radio_scroll);

// Say that a station waits for its screen, before its stream starts. The playing
// screen then says that it connects, in place of the metadata of the station that
// played before.
void RadioUI_setWaitingToStart(bool waiting);

// Render the radio playing screen
void render_radio_playing(SDL_Surface* screen, int show_setting, int radio_selected);

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

// GPU buffer indicator and status functions (rendered independently like Spectrum/PlayTime)
// `row_bottom_y` is the bottom margin of the screen. The renderer measures the text
// that the row holds and puts the foot of the row on that line.
void RadioStatus_setPosition(int bar_x, int bar_w, int bar_h,
                              int left_x, int row_bottom_y);
void RadioStatus_clear(void);
bool RadioStatus_needsRefresh(void);
bool RadioStatus_renderGPU(void);

// The row of the state, as painted onto the overlay layer of the playing screen.
// The painter of that layer calls these, and it draws the spectrum first.
bool RadioStatus_isShowing(void);
void RadioStatus_paint(int layer);

// True while a title of a list of this module moves.
bool radio_list_needs_scroll_refresh(void);

// True while the scroll waits to start and needs one more frame.
bool radio_list_scroll_needs_render(void);

// Move the title without a redraw of the screen.
void radio_list_animate_scroll(void);

// Forget the title that moves. Call this on the way out of a list.
void radio_list_clear_scroll(void);

#endif
