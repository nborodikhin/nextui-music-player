#ifndef __UI_KEYBOARD_H__
#define __UI_KEYBOARD_H__

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "keyboard.h"
#include "keyboard_map.h"

// Must be called before the first use of keyboard.
void UIKeyboard_init(void);

// Release the resources the keyboard holds.
void UIKeyboard_quit(void);

// Everything the keyboard screen draws: the cursor, what it types there, how
// the key under it is drawn, and the text built so far
typedef struct {
    const char*           title;
    const char*           text;
    int                   show_setting;
    int                   row;
    int                   col;
    const KeyboardLayout* layout;
    const char*           lang_label;   // drawn on the lang key: where it leads
    ShiftState            shift;
    bool                  pressed;   // the key under the cursor is held down
    bool                  picking_variant;   // the alternates of the current key are up
    int                   current_variant;   // the alternate being picked, while they are
} KeyboardUiState;

// Compare states for equality, could be used to avoid extra rendering.
// Note: it is shallow comparison, pointers must remain the same to be considered equal.
bool UIKeyboard_stateEquals(const KeyboardUiState* a, const KeyboardUiState* b);

// Draw the keyboard screen.
void UIKeyboard_render(SDL_Surface* screen, const KeyboardUiState* state);

#endif
