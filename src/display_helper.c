#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "display_helper.h"
#include "watchdog.h"

// New screen surface after TG5050 display recovery (NULL = no recovery needed)
static SDL_Surface* reinit_screen = NULL;

// Whether display has been released for an external binary
static bool display_released = false;

// Defined in generic_video.c — only the video pipeline, no fonts/config/assets.
extern void PLAT_quitVideo(void);
extern SDL_Surface* PLAT_initVideo(void);

void DisplayHelper_prepareForExternal(void) {
	if (strcmp(PLATFORM, "tg5050") != 0)
		return;

	// Display teardown can take noticeable wall-clock time and blocks main —
	// pair this with the recovery side and the keyboard call between them so
	// the watchdog stays muzzled for the entire prepare → external → recover
	// window.
	Watchdog_pause(WATCHDOG_REASON_DISPLAY_RECOVERY, true);

	// Keep SDL alive during video subsystem teardown.
	// PLAT_quitVideo calls SDL_QuitSubSystem(SDL_INIT_VIDEO) — if no other
	// subsystem is alive, SDL would fully quit and lose all state.
	SDL_InitSubSystem(SDL_INIT_EVENTS);

	PLAT_quitVideo();
	display_released = true;
}

void DisplayHelper_recoverDisplay(void) {
	if (!display_released) {
		// Either we're on tg5040 (prepare was a no-op) or recover was called
		// without a matching prepare. In both cases there's nothing to do —
		// and no Watchdog_pause/resume to balance.
		return;
	}

	reinit_screen = PLAT_initVideo();

	SDL_QuitSubSystem(SDL_INIT_EVENTS);
	display_released = false;

	// Closes the pause window opened by DisplayHelper_prepareForExternal.
	Watchdog_resume(WATCHDOG_REASON_DISPLAY_RECOVERY, true);
}

SDL_Surface* DisplayHelper_getReinitScreen(void) {
	return reinit_screen;
}
