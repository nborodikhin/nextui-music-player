#ifndef __MODULE_MENU_H__
#define __MODULE_MENU_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "menu_rows.h"

// Run the main menu. Returns the row the user chose, or MENU_QUIT.
MenuSelection MenuModule_run(DisplayContext* display);

#ifdef __cplusplus
}
#endif

#endif
