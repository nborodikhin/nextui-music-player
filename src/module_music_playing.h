#ifndef __MODULE_MUSIC_PLAYING_H__
#define __MODULE_MUSIC_PLAYING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "display_helper.h"
#include "module_common.h"

// Runs the playing screen of the music for the queue that the music session plays. Returns
// MODULE_EXIT_BACK for B and for the end of the queue, and MODULE_EXIT_QUIT for a quit. Sets
// `*queue_ended` to true when the screen closed because the queue ended.
ModuleExitReason MusicPlayingModule_run(DisplayContext* display, bool* queue_ended);

#ifdef __cplusplus
}
#endif

#endif
