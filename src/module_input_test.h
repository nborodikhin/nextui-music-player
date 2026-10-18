#ifndef __MODULE_INPUT_TEST_H__
#define __MODULE_INPUT_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG

#include "display_helper.h"
#include "module_common.h"

// Runs the input test screen of the debug kind: it shows the held buttons and the last raw
// events and button events. MENU + SELECT returns MODULE_EXIT_BACK.
ModuleExitReason InputTestModule_run(DisplayContext* display);

#endif

#ifdef __cplusplus
}
#endif

#endif
