#ifndef __MODULE_RADIO_MANAGE_H__
#define __MODULE_RADIO_MANAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "display_helper.h"
#include "module_common.h"

// Runs Manage Stations: the country list, the stations of a country, and Radio Help. Returns
// MODULE_EXIT_BACK for B on the country list, and MODULE_EXIT_QUIT for a quit.
ModuleExitReason RadioManageModule_run(DisplayContext* display);

#ifdef __cplusplus
}
#endif

#endif
