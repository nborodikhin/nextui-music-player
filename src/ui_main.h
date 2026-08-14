#ifndef __UI_MAIN_H__
#define __UI_MAIN_H__

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#include "menu_rows.h"

// Render the main menu.
void render_menu(SDL_Surface* screen, int show_setting, int menu_selected, int menu_scroll,
                 char* toast_message, uint32_t toast_time, const MenuRows* rows);

// Render confirmation dialog overlay (title + optional content + "A: Yes  B: No")
void render_confirmation_dialog(SDL_Surface* screen, const char* content, const char* title);

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, int app_state);

// Render screen off hint message
void render_screen_off_hint(SDL_Surface* screen);

// Check if Resume scroll needs continuous redraw (software scroll mode)
bool menu_needs_scroll_redraw(void);

#endif
