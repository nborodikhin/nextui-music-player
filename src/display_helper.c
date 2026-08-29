#include <stdbool.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "display_helper.h"

#define MAX_RECREATED_CALLBACKS 8

// Time for the button that launched an external program to be released, so
// neither that program nor we mistake the held press for input of our own.
#define INPUT_SETTLE_MS 100

// Dimensions are cached rather than read off the surface: they stay queryable
// while the display is released for an external binary, when there is no
// surface. Only code that actually draws needs the surface itself.
struct DisplayContext {
	SDL_Surface* surface;
	int width;
	int height;
};

static DisplayContext context;
static DisplayRecreatedCallback recreated_callbacks[MAX_RECREATED_CALLBACKS];

// Whether display has been released for an external binary
static bool display_released = false;

// Defined in generic_video.c — only the video pipeline, no fonts/config/assets.
extern void PLAT_quitVideo(void);
extern SDL_Surface* PLAT_initVideo(void);

DisplayContext* DisplayHelper_init(SDL_Surface* screen) {
	context.surface = screen;
	context.width = screen ? screen->w : 0;
	context.height = screen ? screen->h : 0;
	return &context;
}

DisplayContext* DisplayHelper_current(void) {
	return &context;
}

SDL_Surface* DisplayHelper_getSurface(const DisplayContext* display) {
	return display ? display->surface : NULL;
}

int DisplayHelper_getWidth(const DisplayContext* display) {
	return display ? display->width : 0;
}

int DisplayHelper_getHeight(const DisplayContext* display) {
	return display ? display->height : 0;
}

int DisplayHelper_addRecreatedCallback(DisplayRecreatedCallback callback) {
	if (!callback) return -1;

	for (int i = 0; i < MAX_RECREATED_CALLBACKS; i++) {
		if (recreated_callbacks[i] == callback) return 0;
	}

	for (int i = 0; i < MAX_RECREATED_CALLBACKS; i++) {
		if (!recreated_callbacks[i]) {
			recreated_callbacks[i] = callback;
			return 0;
		}
	}
	return -1;
}

void DisplayHelper_removeRecreatedCallback(DisplayRecreatedCallback callback) {
	for (int i = 0; i < MAX_RECREATED_CALLBACKS; i++) {
		if (recreated_callbacks[i] == callback) recreated_callbacks[i] = NULL;
	}
}

static void notify_recreated(void) {
	for (int i = 0; i < MAX_RECREATED_CALLBACKS; i++) {
		if (recreated_callbacks[i]) recreated_callbacks[i]();
	}
}

void DisplayHelper_prepareForExternal(void) {
	// Clear our data to avoid any chance of flickering.
	if (context.surface) GFX_clearAll();

	// The launching press is probably still held - let it go before the external
	// program starts reading input and takes it as its own.
	SDL_Delay(INPUT_SETTLE_MS);
	PAD_poll();
	PAD_reset();

	// Keep SDL alive during video subsystem teardown.
	// PLAT_quitVideo calls SDL_QuitSubSystem(SDL_INIT_VIDEO) — if no other
	// subsystem is alive, SDL would fully quit and lose all state.
	SDL_InitSubSystem(SDL_INIT_EVENTS);

	PLAT_quitVideo();
	context.surface = NULL;
	display_released = true;
}

void DisplayHelper_recoverDisplay(void) {
	if (!display_released)
		return;

	SDL_Surface* screen = PLAT_initVideo();
	if (!screen)
		return;

	context.surface = screen;
	context.width = screen->w;
	context.height = screen->h;
	GFX_clearAll();

	notify_recreated();

	SDL_QuitSubSystem(SDL_INIT_EVENTS);
	display_released = false;

	// Drop whatever the external program left behind, including the press that
	// dismissed it.
	SDL_Delay(INPUT_SETTLE_MS);
	PAD_poll();
	PAD_reset();
}
