#ifndef __MODULE_PLAYLIST_H__
#define __MODULE_PLAYLIST_H__

#include <SDL2/SDL.h>
#include "module_common.h"
typedef struct DisplayContext DisplayContext;

// Run the playlist module (list → detail → playing)
ModuleExitReason PlaylistModule_run(DisplayContext* display);

#endif
