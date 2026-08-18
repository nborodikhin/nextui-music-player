#ifndef __MODULE_SETTINGS_H__
#define __MODULE_SETTINGS_H__

#include <SDL2/SDL.h>
#include "module_common.h"
typedef struct DisplayContext DisplayContext;

// Run the settings module
// Handles: Settings menu, About screen, app updates
ModuleExitReason SettingsModule_run(DisplayContext* display);

#endif
