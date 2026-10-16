#ifndef __MODULE_PLAYLIST_H__
#define __MODULE_PLAYLIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "module_common.h"

// Run the playlist module (list → detail → playing)
ModuleExitReason PlaylistModule_run(DisplayContext* display);

#ifdef __cplusplus
}
#endif

#endif
