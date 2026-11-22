#ifndef __MODULE_RADIO_PLAYING_H__
#define __MODULE_RADIO_PLAYING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "display_helper.h"
#include "module_common.h"

// Runs the playing screen of the radio. Pass the station that the user selected in `url`, or NULL
// to show the station that plays in the background. Writes the station that the screen showed
// last into `shown_url`, which holds RADIO_MAX_URL bytes. Returns MODULE_EXIT_BACK for B, and
// MODULE_EXIT_QUIT for a quit.
ModuleExitReason RadioPlayingModule_run(DisplayContext* display, const char* url, char* shown_url);

#ifdef __cplusplus
}
#endif

#endif
