#ifndef __MODULE_SETTINGS_H__
#define __MODULE_SETTINGS_H__

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "module_common.h"

// Run the settings module
// Handles: Settings menu, About screen, app updates
ModuleExitReason SettingsModule_run(DisplayContext* display);
void SettingsModule_migrateData(void);

void SettingsModule_cycleScreenOffNext(void);
void SettingsModule_cycleScreenOffPrev(void);
const char* SettingsModule_getScreenOffDisplayStr(void);

void SettingsModule_cycleBassFilterNext(void);
void SettingsModule_cycleBassFilterPrev(void);
const char* SettingsModule_getBassFilterDisplayStr(void);

void SettingsModule_cycleSoftLimiterNext(void);
void SettingsModule_cycleSoftLimiterPrev(void);
const char* SettingsModule_getSoftLimiterDisplayStr(void);

void SettingsModule_toggleAutoUpdate(void);
const char* SettingsModule_getAutoUpdateDisplayStr(void);

#endif
