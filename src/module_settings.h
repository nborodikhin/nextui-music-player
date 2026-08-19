#ifndef __MODULE_SETTINGS_H__
#define __MODULE_SETTINGS_H__

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "module_common.h"

// Run the settings module
// Handles: Settings menu, About screen, app updates
ModuleExitReason SettingsModule_run(DisplayContext* display);

#endif
