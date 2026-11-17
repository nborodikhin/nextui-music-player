#ifndef __MODULE_FILE_BROWSER_H__
#define __MODULE_FILE_BROWSER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "display_helper.h"
#include "module_common.h"

// Runs the file browser of the music folder. A file or Play All starts the queue of its folder
// and opens the playing screen.
ModuleExitReason FileBrowserModule_run(DisplayContext* display);

void FileBrowserModule_quit(void);

#ifdef __cplusplus
}
#endif

#endif
