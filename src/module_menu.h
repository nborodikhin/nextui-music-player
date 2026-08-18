#ifndef __MODULE_MENU_H__
#define __MODULE_MENU_H__

#include <SDL2/SDL.h>

#include "menu_rows.h"
typedef struct DisplayContext DisplayContext;

// Run the main menu. Returns the row the user chose, or MENU_QUIT.
MenuSelection MenuModule_run(DisplayContext* display);

#endif
