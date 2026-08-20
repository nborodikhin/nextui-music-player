#ifndef __MODULE_DOWNLOADER_H__
#define __MODULE_DOWNLOADER_H__

#include <SDL2/SDL.h>

#include "display_helper.h"
#include "module_common.h"

// Outcome of the yt-dlp install/update screen
typedef enum {
    YTDLP_INSTALL_DONE,   // screen finished; check Downloader_isAvailable() for the result
    YTDLP_INSTALL_QUIT    // user asked to exit the app
} YtdlpInstallResult;

// Run the downloader (YouTube) module
// Handles: Search, results, queue, downloading, yt-dlp updates
ModuleExitReason DownloaderModule_run(DisplayContext* display);

// Download and install yt-dlp, or update an installed one, showing progress.
// Blocks until the operation finishes or the user cancels with B.
YtdlpInstallResult DownloaderModule_runInstall(DisplayContext* display, int* show_setting);

#endif
