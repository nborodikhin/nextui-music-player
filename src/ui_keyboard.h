#ifndef __UI_KEYBOARD_H__
#define __UI_KEYBOARD_H__

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "keyboard_map.h"

// Probe the font once and drop the alternates it cannot draw. Called before
// the first frame, so no frame pays for the scan.
void UIKeyboard_init(void);

// Draw the keyboard screen : prompt as the screen title, the text field, the key
// grid scaled to the space between header and button hints, and the hints.
// pressed draws the cursor's key as held down. A variant popup is drawn over it
// while variant >= 0, with that alternate highlighted.
void UIKeyboard_render(SDL_Surface* screen, const char* prompt, const char* text,
                       int map, ShiftState shift, int row, int col, bool pressed,
                       int variant, int show_setting);

#endif
