#ifndef __MODULE_AUDIOBOOK_H__
#define __MODULE_AUDIOBOOK_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "module_common.h"
#include "display_helper.h"

// Blocking screen controller for the audiobook library / player
ModuleExitReason AudiobookModule_run(DisplayContext* display);

// True while a book is loaded and not stopped
bool AudiobookModule_isActive(void);

// Chapter advancement, periodic saving and the sleep timer, run from the main
// menu while a book plays in the background
void AudiobookModule_backgroundTick(void);

// Save progress and stop playback (called from Background_stopAll)
void AudiobookModule_stop(void);

#endif
