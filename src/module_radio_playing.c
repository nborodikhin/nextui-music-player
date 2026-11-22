#include "module_radio_playing.h"

#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "album_art.h"
#include "background.h"
#include "help_screen.h"
#include "player.h"
#include "radio.h"
#include "spectrum.h"
#include "ui_album_art.h"
#include "ui_layers.h"
#include "ui_main.h"
#include "ui_radio_playing.h"
#include "ui_utils.h"

// A station that waits for the playing screen. `Radio_play()` stops the stream that runs, and
// that stop joins the thread of the stream. The screen starts the new station after it drew a
// frame, thus the wait for the stream happens on the screen of the station that the user chose.
static char pending_url[RADIO_MAX_URL] = "";
static bool screen_drawn = false;
static bool screen_off = false;

// The metadata of the last draw, to find a change
static char last_rendered_artist[256] = "";
static char last_rendered_title[256] = "";
static bool last_art_was_fetching = false;

// Gives up a station that waits, thus a screen that the user leaves does not hold a station that
// never starts.
static void cancel_pending_station(void) {
    pending_url[0] = '\0';
    RadioUI_setWaitingToStart(false);
}

// Takes the old station down and gives `url` to the screen as the station that waits. Pass true
// in `keep_bars` where the screen stays, thus the bars of the old station fall on it.
static void hand_station_to_screen(const char* url, bool keep_bars) {
    Radio_stop();
    Radio_clearMetadata();
    if (!keep_bars) Spectrum_reset();

    snprintf(pending_url, sizeof(pending_url), "%s", url);
    RadioUI_setWaitingToStart(true);
}

// Returns the station that the screen shows: the station that waits, else the station of the
// radio. Returns NULL with no station.
static const char* get_screen_url(void) {
    if (pending_url[0]) return pending_url;
    const char* current = Radio_getCurrentUrl();
    return current && current[0] ? current : NULL;
}

static int find_station_index(const char* url) {
    if (!url) return -1;
    RadioStation* stations;
    int count = Radio_getStations(&stations);
    for (int i = 0; i < count; i++) {
        if (strcmp(stations[i].url, url) == 0) return i;
    }
    return -1;
}

static void step_station(bool next) {
    RadioStation* stations;
    int count = Radio_getStations(&stations);
    int index = Radio_pickStation(stations, count, get_screen_url(), next);
    if (index >= 0) hand_station_to_screen(stations[index].url, true);
}

// The USB and Bluetooth buttons while the screen is off or the hint shows. They act as on a screen
// that draws.
static void handle_hid_events(void) {
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        ModuleCommon_handleMediaKey(hid_event);
    }
}

// The layers of the playing screen, once for each frame of the loop. A frame that redraws the
// screen paints them after that redraw, thus this one leaves them to the render block.
static void refresh_gpu_layers(int* dirty) {
    if (ModuleCommon_isScreenOffHintActive()) return;
    if (RadioPlaying_frame(*dirty != 0)) *dirty = 1;
}

// A display recreate drops every GPU layer. The next frame paints each one again, whatever the
// caches of the screen say.
static void display_recreated(void) {
    RadioPlaying_invalidate();
    // A new display is on and has no content
    screen_off = false;
}

static void open_screen(void) {
    DisplayHelper_addRecreatedCallback(display_recreated);
    Spectrum_init();
    screen_off   = false;
    screen_drawn = false;
    last_rendered_artist[0] = '\0';
    last_rendered_title[0]  = '\0';
    last_art_was_fetching   = false;
    UiLayer_clear(UI_LAYER_ANIMATION);
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();
    ModuleCommon_setAutosleepDisabled(true);
}

// The one exit of the screen, for B and for a quit
static ModuleExitReason close_screen(ModuleExitReason reason, char* shown_url) {
    if (shown_url) {
        const char* url = get_screen_url();
        snprintf(shown_url, RADIO_MAX_URL, "%s", url ? url : "");
    }
    cancel_pending_station();
    if (screen_off) {
        screen_off = false;
        PLAT_enableBacklight(1);
    }
    ModuleCommon_resetScreenOffHint();
    cleanup_album_art_background();
    RadioPlaying_leave();
    Spectrum_quit();
    DisplayHelper_removeRecreatedCallback(display_recreated);
    return reason;
}

// Starts `url`, or takes back the station that plays when it is `url` or when `url` is NULL.
static void take_station(const char* url) {
    pending_url[0] = '\0';
    const char* current = Radio_getCurrentUrl();
    bool plays = Radio_isActive();
    if (!url || (plays && current && strcmp(current, url) == 0)) {
        // `Background_stopAll()` would stop this very station
        if (plays && Background_getActive() == BG_RADIO) Background_setActive(BG_NONE);
        return;
    }
    Background_stopAll();
    hand_station_to_screen(url, false);
}

ModuleExitReason RadioPlayingModule_run(DisplayContext* display, const char* url, char* shown_url) {
    if (shown_url) shown_url[0] = '\0';
    if (!display) return MODULE_EXIT_BACK;

    take_station(url && url[0] ? url : NULL);
    open_screen();
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        ModuleCommon_frameBegin(true);
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // Handle global input (skip if screen off or hint active)
        // A signal must reach the global input, thus a pending stop passes this guard.
        // ModuleCommon_handleGlobalInput() reports the quit and returns before it reads any input,
        // thus the screen-off states keep their behavior.
        if (ModuleCommon_stopSignalled()
            || (!screen_off && !ModuleCommon_isScreenOffHintActive())) {
            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_RADIO_PLAYING);
            if (global.should_quit) {
                Radio_stop();
                return close_screen(MODULE_EXIT_QUIT, shown_url);
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                ModuleCommon_frameEnd(screen);
                continue;
            }
        }

        // The screen is up, thus the wait for the station happens on it. A screen that draws
        // nothing - off, or under the hint - has no bars to fall and no frame to wait for, thus the
        // station starts at once. The USB and Bluetooth buttons of such a screen come first, thus
        // a station that they start replaces the station that waits.
        bool screen_draws = !screen_off && !ModuleCommon_isScreenOffHintActive();
        if (!screen_draws) handle_hid_events();
        if (pending_url[0] && Radio_isActive()) {
            // Another path started the radio while the station waited
            cancel_pending_station();
            dirty = 1;
        }
        bool bars_are_gone = !screen_draws || (screen_drawn && !Spectrum_isShowing());
        // A and B of this frame act on the station that waits, thus it must not start first
        bool gives_up_wait = screen_draws && !ModuleCommon_isKeyLocked()
                             && (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B));
        if (pending_url[0] && bars_are_gone && !gives_up_wait) {
            // `Radio_play()` holds this thread for the whole of the connection, thus no frame
            // draws while it runs. Put the row of the state on the display first, so the screen
            // says that it connects for that time. The frame presents here, before the block, and
            // the pass that follows presents its own.
            if (screen_draws) {
                RadioPlaying_frame(false);
                ModuleCommon_frameEnd(screen);
            }

            Radio_play(pending_url);
            cancel_pending_station();
            dirty = 1;
        }

        ModuleCommon_setAutosleepDisabled(true);

        // Handle screen off hint
        if (ModuleCommon_isScreenOffHintActive()) {
            Radio_update();

            // SELECT+A during hint -> full wake
            if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                ModuleCommon_resetScreenOffHint();
                ModuleCommon_recordInputTime();
                dirty = 1;
                continue;
            }
            // Any other button resets the hint timer
            if (PAD_anyPressed()) {
                ModuleCommon_startScreenOffHint();
            }
            if (ModuleCommon_processScreenOffHintTimeout()) {
                screen_off = true;
                GFX_clear(screen);
                ModuleCommon_markSurfaceDrawn();
            }
            ModuleCommon_frameEnd(screen);
            continue;
        }

        // Handle screen off
        if (screen_off) {
            Radio_update();

            // Any button -> show hint
            if (PAD_anyPressed()) {
                screen_off = false;
                PLAT_enableBacklight(1);
                ModuleCommon_startScreenOffHint();
                GFX_clear(screen);
                render_screen_off_hint(screen);
                ModuleCommon_markSurfaceDrawn();
            }
            ModuleCommon_frameEnd(screen);
            continue;
        }

        // Reset input timer on any button press
        bool key_locked = ModuleCommon_isKeyLocked();
        if (!key_locked && PAD_anyPressed()) {
            ModuleCommon_recordInputTime();
        }

        if (key_locked) {
            // the screen draws, and the keys do nothing
        }
        else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
            step_station(true);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
            step_station(false);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_B)) {
            const bool plays = Radio_isActive();
            ModuleExitReason reason = close_screen(MODULE_EXIT_BACK, shown_url);
            if (plays) {
                Background_setActive(BG_RADIO);
            } else {
                ModuleCommon_setAutosleepDisabled(false);
            }
            return reason;
        }
        else if (PAD_justPressed(BTN_A)) {
            // A toggles play/pause
            if (pending_url[0]) {
                // A station waits for its stream. The radio is not active yet, thus the branch
                // below would read the station that went and start that one. Give up the wait
                // instead.
                cancel_pending_station();
                Radio_clearMetadata();
                dirty = 1;
            }
            else if (Radio_isActive()) {
                // Playing - stop it
                Radio_stop();
                dirty = 1;
            } else {
                // Stopped - resume playing
                const char* current = get_screen_url();
                if (current) {
                    char resume_url[RADIO_MAX_URL];
                    snprintf(resume_url, sizeof(resume_url), "%s", current);
                    hand_station_to_screen(resume_url, true);
                    dirty = 1;
                }
            }
        }
        else if (PAD_justPressed(BTN_L3) || PAD_justPressed(BTN_L2)) {
            Spectrum_cycleNext();
            dirty = 1;
        }
        else if (PAD_tappedSelect(SDL_GetTicks())) {
            ModuleCommon_startScreenOffHint();
            if (!ModuleCommon_shouldDrawContentWhileLocked()) RadioPlaying_leave();
            dirty = 1;
        }

        Radio_update();

        // Check if metadata or album art changed (updated by stream thread)
        {
            const RadioMetadata* meta = Radio_getMetadata();
            bool fetching = album_art_is_fetching();
            if (strcmp(last_rendered_artist, meta->artist) != 0 ||
                strcmp(last_rendered_title, meta->title) != 0 ||
                (last_art_was_fetching && !fetching)) {
                dirty = 1;
            }
            last_art_was_fetching = fetching;
        }

        // Auto screen-off after inactivity
        if (Radio_getState() == RADIO_STATE_PLAYING && ModuleCommon_checkAutoScreenOffTimeout()) {
            RadioPlaying_leave();
            dirty = 1;
        }

        refresh_gpu_layers(&dirty);

        // Handle power management
        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            ModuleCommon_PWR_update(&dirty, &show_setting);
        }

        // Render
        if (dirty && !screen_off) {
            if (ModuleCommon_isScreenOffHintActive()) {
                GFX_clear(screen);
                render_screen_off_hint(screen);
            } else {
                render_radio_playing(screen, show_setting, find_station_index(get_screen_url()));
                RadioPlaying_paintLayers();
                screen_drawn = true;
                const RadioMetadata* meta = Radio_getMetadata();
                snprintf(last_rendered_artist, sizeof(last_rendered_artist), "%s", meta->artist);
                snprintf(last_rendered_title, sizeof(last_rendered_title), "%s", meta->title);
            }

            ModuleCommon_markSurfaceDrawn();
            dirty = 0;
        }
        ModuleCommon_frameEnd(screen);
    }
}
