#ifndef __MODULE_LIBRARY_H__
#define __MODULE_LIBRARY_H__

#include <SDL2/SDL.h>
#include "module_common.h"
typedef struct DisplayContext DisplayContext;

// Run the library submenu (Files, Playlists, Downloader)
ModuleExitReason LibraryModule_run(DisplayContext* display);

#endif
