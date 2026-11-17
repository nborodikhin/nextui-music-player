#ifndef __MODULE_ABOUT_H__
#define __MODULE_ABOUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "display_helper.h"
#include "module_common.h"

// Runs the About screen and the app update. Returns MODULE_EXIT_QUIT for a quit or a restart
// after an update, and MODULE_EXIT_TO_MENU when the user goes back.
ModuleExitReason AboutModule_run(DisplayContext* display);

#ifdef __cplusplus
}
#endif

#endif
