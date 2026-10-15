#ifndef __UI_SYSTEM_H__
#define __UI_SYSTEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>

// Render the app update screen
void render_app_updating(SDL_Surface* screen, int show_setting);

// Render the about screen
void render_about(SDL_Surface* screen, int show_setting);

#ifdef __cplusplus
}
#endif

#endif
