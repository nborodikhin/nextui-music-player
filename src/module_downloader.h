#ifndef __MODULE_DOWNLOADER_H__
#define __MODULE_DOWNLOADER_H__

#include <SDL2/SDL.h>
#include "module_common.h"
typedef struct DisplayContext DisplayContext;

// Run the downloader (YouTube) module
// Handles: Search, results, queue, downloading, yt-dlp updates
ModuleExitReason DownloaderModule_run(DisplayContext* display);

#endif
