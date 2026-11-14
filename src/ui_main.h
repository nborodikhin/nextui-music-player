#ifndef __UI_MAIN_H__
#define __UI_MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#include "help_screen.h"

// Render confirmation dialog overlay (title + optional content + "A: Yes  B: No")
void render_confirmation_dialog(SDL_Surface* screen, const char* content, const char* title);

// One row of a controls help table: a button and what it does
typedef struct {
    const char* button;
    const char* action;
} ControlHelp;

// Returns the controls help table of help_id, which ends with a row of NULLs, and writes its
// title to title. The table and the title stay valid for the life of the app.
const ControlHelp* get_controls_help(HelpId help_id, const char** title);

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, HelpId help_id);

// Render screen off hint message
void render_screen_off_hint(SDL_Surface* screen);

#ifdef __cplusplus
}
#endif

#endif
