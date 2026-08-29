#ifndef __UI_KEYBOARD_H__
#define __UI_KEYBOARD_H__

#include <SDL2/SDL.h>

#include "keyboard_map.h"

// Must be called before the first use of keyboard.
void UIKeyboard_init(void);

// Release the resources the keyboard holds.
void UIKeyboard_quit(void);

// Draw the keyboard screen.
// Variant popup is displayed if selected_variant is >= 0.
void UIKeyboard_render(SDL_Surface* screen, const char* title, const char* text,
                       int map_index, ShiftState shift, int selected_row, int selected_col, bool pressed,
                       int selected_variant, int show_setting);

#endif
