#ifndef __MODULE_MUSIC_FOLDERS_H__
#define __MODULE_MUSIC_FOLDERS_H__

#include <SDL2/SDL.h>
#include "module_common.h"

// Manage the list of music folders: list, add (via folder picker), remove.
ModuleExitReason MusicFoldersModule_run(SDL_Surface* screen);

#endif
