#ifndef __MODULE_DOWNLOADER_H__
#define __MODULE_DOWNLOADER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "module_common.h"

// Run the downloader (YouTube) module
// Handles: Search, results, queue, downloading, yt-dlp updates
ModuleExitReason DownloaderModule_run(DisplayContext* display);

#ifdef __cplusplus
}
#endif

#endif
