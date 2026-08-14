#ifndef __MODULE_MENU_H__
#define __MODULE_MENU_H__

#include <SDL2/SDL.h>

#include "menu_rows.h"

// Run the main menu. Returns the row the user chose, or MENU_QUIT.
MenuSelection MenuModule_run(SDL_Surface* screen);

// Set toast message (called by modules returning to menu with a message)
void MenuModule_setToast(const char* message);

#endif
