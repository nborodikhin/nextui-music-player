#ifndef __MODULE_COMMON_H__
#define __MODULE_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "help_screen.h"
#include "player.h"

// Screen off hint duration (time hint is shown before screen turns off)
#define SCREEN_OFF_HINT_DURATION_MS 4000

// Module exit reasons
typedef enum {
    MODULE_EXIT_NONE,       // Stay on the screen. A zeroed field reads as this value
    MODULE_EXIT_BACK,       // Return to the screen that opened this one
    MODULE_EXIT_TO_MENU,    // User pressed B, return to main menu
    MODULE_EXIT_QUIT        // User confirmed quit, exit app entirely
} ModuleExitReason;

// Result from global input handling
typedef struct {
    bool input_consumed;    // True if global input was handled (dialog shown, etc.)
    bool should_quit;       // True if quit was confirmed
    bool dirty;             // True if screen needs redraw
} GlobalInputResult;

// Initialize module common (call once at app startup)
void ModuleCommon_init(void);

// Handle global input (START dialogs, volume, power management)
// Call at the start of each module's input loop
// Parameters:
//   screen - SDL surface for rendering dialogs
//   show_setting - pointer to show_setting flag (for power hints)
//   help_id - which screen is asking (selects the controls help text)
GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, int* show_setting, HelpId help_id);

// Request the same quit that the confirmation dialog gives. Every module then
// returns MODULE_EXIT_QUIT and the app stops with its usual cleanup.
void ModuleCommon_requestQuit(void);

// Record that a signal asked the app to stop. Safe to call from a signal
// handler: it writes one flag and calls nothing. The next call of
// ModuleCommon_handleGlobalInput() turns it into the quit above, thus the
// module that is in operation returns and the app runs its cleanup.
void ModuleCommon_signalStop(void);

// Returns true once a signal asked the app to stop. The loop between the
// modules reads it, because no module is in operation there to see the quit.
bool ModuleCommon_stopSignalled(void);

// Returns true when the app must quit: a signal, a confirmed Quit dialog, or the control
// channel asked for it. A loop that does not call ModuleCommon_handleGlobalInput() reads it.
bool ModuleCommon_isQuitRequested(void);

// Disable/enable autosleep (for modules with active playback)
void ModuleCommon_setAutosleepDisabled(bool disabled);

// Check if screen off hint is active
bool ModuleCommon_isScreenOffHintActive(void);

// Returns true when the screen keeps its content while the keys are locked, as on
// HDMI, which has no backlight to turn off.
bool ModuleCommon_shouldDrawContentWhileLocked(void);

// Lock the keys. Where the screen goes dark, start the countdown of the screen
// off hint: the caller then removes its GPU layers and draws the hint. Where the
// screen keeps its content (see ModuleCommon_shouldDrawContentWhileLocked()),
// show an unlock toast: the module draws, and skips its input while
// ModuleCommon_isKeyLocked() returns true.
void ModuleCommon_startScreenOffHint(void);

// Returns true while the keys of a screen that keeps its content are locked. The
// module then skips its input. SELECT + A opens the lock.
bool ModuleCommon_isKeyLocked(void);

// Reset (cancel) screen off hint
void ModuleCommon_resetScreenOffHint(void);

// Check screen off hint timeout using dual SDL tick + wallclock check.
// If timed out: deactivates hint and disables backlight. Returns true.
// If still counting down or hint not active: returns false.
bool ModuleCommon_processScreenOffHintTimeout(void);

// Record last input time (for auto screen-off timeout)
void ModuleCommon_recordInputTime(void);

// Feed the progressive seek of LEFT and RIGHT on a playing screen, once each
// frame. A short press seeks by `back_step_ms` or `fwd_step_ms`; a held button
// scans, and its release seeks once. Returns true where the screen must draw.
bool ModuleCommon_updateSeekScan(int back_step_ms, int fwd_step_ms);

// Check if auto screen-off timeout has elapsed since last input.
// If timed out: locks the keys, and returns true where the screen goes dark. The
// caller then removes its GPU layers and draws the hint.
bool ModuleCommon_checkAutoScreenOffTimeout(void);

// Clean up module common resources (call at app exit)
void ModuleCommon_quit(void);

// PWR_update wrapper with overlay auto-hide on button release
// Call this instead of PWR_update directly in modules
void ModuleCommon_PWR_update(int* dirty, int* show_setting);

// As ModuleCommon_PWR_update(), and also sets `woke` to true when the device woke from sleep
// in this call, else to false. The sleep clears the screen.
void ModuleCommon_PWR_updateWithWake(int* dirty, int* show_setting, bool* woke);

// Returns true one time after ModuleCommon_frameBegin() moved the app to a new output. The
// move made the display again, thus the screen holds nothing. A loop that does not call
// ModuleCommon_handleGlobalInput() calls this.
bool ModuleCommon_takeOutputSwitched(void);

// Handle a single HID volume event. Returns true if the event was a volume event.
bool ModuleCommon_handleHIDVolume(USBHIDEvent hid_event);


// Begin one module-loop iteration: start the frame timer, poll input.
// MUST be the first statement of every module's loop body.
void ModuleCommon_frameBegin(void);

// Records that the frame drew the regular surface, thus ModuleCommon_frameEnd()
// uploads it. Call it after a render of a screen or of a dialog.
void ModuleCommon_markSurfaceDrawn(void);

// Records that the frame drew into a GPU layer, thus ModuleCommon_frameEnd()
// presents the layers. UiLayer_clear() and UiLayer_blit() call it, thus a
// painter that draws through them needs no call of its own.
void ModuleCommon_markLayerDrawn(void);

// The one presentation of the app. Call it once at the end of each pass of a
// loop, after all drawing of the frame, and before a call that blocks after a
// draw. A surface change uploads the surface and presents it with the layers.
// A layer change presents the layers over the surface that the display holds.
// No change synchronizes to the frame rate and presents nothing. No painter
// presents by itself.
void ModuleCommon_frameEnd(SDL_Surface* screen);

#ifdef __cplusplus
}
#endif

#endif
