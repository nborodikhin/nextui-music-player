#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "display_helper.h"
#include "pad_reset.h"
#include "selfupdate.h"
#include "ui_theme.h"

#define MAX_RECREATED_CALLBACKS 8

// Time for the button that launched an external program to be released, so
// neither that program nor we mistake the held press for input of our own.
#define INPUT_SETTLE_MS 100

// Interval between reads of the cable state. Each read is a sysfs file read.
#define OUTPUT_POLL_MS 500

// Time for a cable to seat before the app reads its state again.
#define CABLE_SETTLE_MS 1000

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

static void release_video(void) {
	// Keep SDL alive during video subsystem teardown.
	// PLAT_quitVideo calls SDL_QuitSubSystem(SDL_INIT_VIDEO) — if no other
	// subsystem is alive, SDL would fully quit and lose all state.
	SDL_InitSubSystem(SDL_INIT_EVENTS);

	PLAT_quitVideo();
	context.surface = NULL;
	display_released = true;
}

// Restarts the app when the platform gives no surface: the modules cannot run
// without one, and a new start sets up the video again.
static void rebuild_video(void) {
	SDL_Surface* screen = PLAT_initVideo();
	if (!screen) {
		LOG_error("the platform gave no display surface, restarting\n");
		SelfUpdate_requestRestart();
		exit(EXIT_FAILURE);
	}

	context.surface = screen;
	context.width = screen->w;
	context.height = screen->h;
	// PLAT_initVideo() resets the clear color. Set the page background again.
	GFX_setClearColor(Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, false));
	GFX_clearAll();

	notify_recreated();

	SDL_QuitSubSystem(SDL_INIT_EVENTS);
	display_released = false;
}

void DisplayHelper_prepareForExternal(void) {
	// Clear our data to avoid any chance of flickering.
	if (context.surface) GFX_clearAll();

	// The button that triggered the external program could still be pressed.
	// Wait a bit to get it released before the external program starts reading input.
	SDL_Delay(INPUT_SETTLE_MS);
	PAD_poll();
	PadReset_run();

	release_video();
}

void DisplayHelper_recoverDisplay(void) {
	if (!display_released)
		return;

	rebuild_video();

	// Drop whatever the external program left behind, including the press that
	// dismissed it.
	SDL_Delay(INPUT_SETTLE_MS);
	PAD_poll();
	PadReset_run();
}

// Time of the last read of the cable state.
static uint32_t last_output_poll = 0;

bool DisplayHelper_outputChanged(void) {
	if (display_released)
		return false;

	uint32_t now = SDL_GetTicks();
	if (now - last_output_poll < OUTPUT_POLL_MS)
		return false;
	last_output_poll = now;

	return GetHDMI() != hdmi_active;
}

void DisplayHelper_moveToOutput(void) {
	// A cable that is not fully in can connect and disconnect for a short time.
	SDL_Delay(CABLE_SETTLE_MS);
	int has_hdmi = GetHDMI();

	// The teardown and SetHDMI() fill the framebuffer with black. A clear here
	// would show the page background for one frame before the black.
	LOG_info("switching the display to %s\n", has_hdmi ? "HDMI" : "the panel");
	release_video();

	// The macros of the screen size, the refresh rate and the panel rotation
	// read hdmi_active, thus it changes before the platform makes the new surface.
	hdmi_active = has_hdmi;
	PLAT_initPlatform();
	SetHDMI(hdmi_active);

	rebuild_video();
	// The panel can be dark from the screen-off of a module
	if (!hdmi_active)
		PLAT_enableBacklight(1);

	last_output_poll = SDL_GetTicks();
	PadReset_run();
}
