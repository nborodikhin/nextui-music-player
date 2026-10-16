#ifndef __MODULE_RADIO_H__
#define __MODULE_RADIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "module_common.h"

// Run the online radio module
// Handles: Station list, playback, adding stations from curated list
ModuleExitReason RadioModule_run(DisplayContext* display);

#ifdef __cplusplus
}
#endif

#endif
