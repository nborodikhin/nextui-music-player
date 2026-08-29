#ifndef __UI_KEYBOARD_H__
#define __UI_KEYBOARD_H__

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "keyboard_map.h"

// The text field sits above the grid, so it is the row before the first one.
// Nothing navigates there yet; a cursor inside the field would.
#define KEYBOARD_INPUT_ROW (-1)

// Probe the font once and drop the alternates it cannot draw. Called before
// the first frame, so no frame pays for the scan.
void UIKeyboard_init(void);

// Draw the keyboard screen.
// Variant popup is displayed if selected_variant is >= 0.
void UIKeyboard_render(SDL_Surface* screen, const char* title, const char* text,
                       int map_index, ShiftState shift, int row, int col, bool pressed,
                       int selected_variant, int show_setting);

#endif
