#include <string.h>
#include <time.h>
#include <msettings.h>
#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_player.h"
#include "settings.h"
#include "ui_main.h"
#include "ui_music.h"
#include "watchdog.h"
#include "crash_handler.h"
#include "log_trace.h"
#include "ui_radio.h"
#include "player.h"
#include "radio.h"
#include "spectrum.h"
#include "background.h"

static bool autosleep_disabled = false;
static uint32_t last_input_time = 0;

// Screen off hint state
static bool screen_off_hint_active = false;
static uint32_t screen_off_hint_start = 0;
static time_t screen_off_hint_start_wallclock = 0;

// Dialog states
static bool show_quit_confirm = false;
static bool show_controls_help = false;

// START button long press detection
static uint32_t start_press_time = 0;
static bool start_was_pressed = false;
#define START_LONG_PRESS_MS 500

// Overlay state tracking - force hide after button release
static bool overlay_buttons_were_active = false;
static uint32_t overlay_release_time = 0;
#define OVERLAY_VISIBLE_AFTER_RELEASE_MS 800  // How long overlay stays visible after release
#define OVERLAY_FORCE_HIDE_DURATION_MS 500    // How long to keep forcing hide

void ModuleCommon_tickToast(char* message, uint32_t toast_time, int* dirty) {
    if (message[0] == '\0') return;
    if (SDL_GetTicks() - toast_time < TOAST_DURATION) {
        *dirty = 1;
    } else {
        message[0] = '\0';
        *dirty = 1;
    }
}

void ModuleCommon_init(void) {
    autosleep_disabled = false;
    last_input_time = SDL_GetTicks();
    screen_off_hint_active = false;
    show_quit_confirm = false;
    show_controls_help = false;
    start_was_pressed = false;
    overlay_buttons_were_active = false;
    overlay_release_time = 0;
}

GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, int* show_setting, int app_state) {
    GlobalInputResult result = {false, false, false};

    // Poll USB HID events (earphone buttons)
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (ModuleCommon_handleHIDVolume(hid_event)) {
            result.dirty = true;
            result.input_consumed = true;
        }
        else if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
            // Check what's currently playing and handle accordingly
            // Check radio first (takes priority when streaming)
            RadioState radio_state = Radio_getState();
            PlayerState player_state = Player_getState();

            if (radio_state == RADIO_STATE_PLAYING || radio_state == RADIO_STATE_BUFFERING) {
                // Radio is streaming - stop it
                Radio_stop();
                result.dirty = true;
                result.input_consumed = true;
            }
            else if (player_state == PLAYER_STATE_PLAYING || player_state == PLAYER_STATE_PAUSED) {
                // Music player is active - toggle pause
                Player_togglePause();
                result.dirty = true;
                result.input_consumed = true;
            }
            else {
                // Nothing playing - check if we can resume radio
                const char* last_url = Radio_getCurrentUrl();
                if (last_url && last_url[0] != '\0') {
                    // Resume last radio station
                    Radio_play(last_url);
                    result.dirty = true;
                    result.input_consumed = true;
                }
            }
        }
        else if (hid_event == USB_HID_EVENT_NEXT_TRACK || hid_event == USB_HID_EVENT_PREV_TRACK) {
            // Next/previous track - check what's currently active
            RadioState radio_state = Radio_getState();

            if (radio_state == RADIO_STATE_PLAYING || radio_state == RADIO_STATE_BUFFERING || radio_state == RADIO_STATE_CONNECTING) {
                // Radio is active - switch stations
                RadioStation* stations;
                int station_count = Radio_getStations(&stations);
                if (station_count > 1) {
                    // Find current station index
                    int current_idx = Radio_findCurrentStationIndex();
                    if (current_idx < 0) continue;
                    // Calculate new index
                    int new_idx;
                    if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
                        new_idx = (current_idx + 1) % station_count;
                    } else {
                        new_idx = (current_idx - 1 + station_count) % station_count;
                    }
                    // Switch to new station
                    Radio_stop();
                    Radio_play(stations[new_idx].url);
                    result.dirty = true;
                    result.input_consumed = true;
                }
            }
            else if (PlayerModule_isActive()) {
                // Music player is active - next/previous song
                if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
                    PlayerModule_nextTrack();
                } else {
                    PlayerModule_prevTrack();
                }
                result.dirty = true;
                result.input_consumed = true;
            }
        }
    }

    // Handle volume controls - only when NOT in a combo with MENU or SELECT
    // (Menu + Vol = brightness, Select + Vol = color temp - handled by platform)
    // Note: We don't consume input or return early here - let PWR_update detect
    // the volume button press and set show_setting to display the volume UI
    ModuleCommon_handleHardwareVolume();

    // Handle quit confirmation dialog
    if (show_quit_confirm) {
        if (PAD_justPressed(BTN_A)) {
            LOG_trace("dialog exit: quit_confirm action=quit");
            show_quit_confirm = false;
            result.input_consumed = true;
            result.should_quit = true;
            return result;
        }
        else if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_START)) {
            LOG_trace("dialog exit: quit_confirm action=cancel");
            show_quit_confirm = false;
            result.input_consumed = true;
            result.dirty = true;
            return result;
        }
        // Dialog is shown, consume input and render (covers entire screen)
        render_confirmation_dialog(screen, NULL, "Quit Music Player?");
        GFX_flip(screen);
        result.input_consumed = true;
        return result;
    }

    // Handle controls help dialog - press any button to close
    if (show_controls_help) {
        if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B) || PAD_justPressed(BTN_X) ||
            PAD_justPressed(BTN_Y) || PAD_justPressed(BTN_START) || PAD_justPressed(BTN_SELECT) ||
            PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_DOWN) ||
            PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT) ||
            PAD_justPressed(BTN_L1) || PAD_justPressed(BTN_R1) || PAD_justPressed(BTN_MENU)) {
            LOG_trace("dialog exit: controls_help");
            show_controls_help = false;
            result.input_consumed = true;
            result.dirty = true;
            return result;
        }
        // Dialog is shown, consume input and render (covers entire screen)
        render_controls_help(screen, app_state);
        GFX_flip(screen);
        result.input_consumed = true;
        return result;
    }

    // Handle START button - track press time for short/long press detection
    if (PAD_justPressed(BTN_START)) {
        start_press_time = SDL_GetTicks();
        start_was_pressed = true;
        result.input_consumed = true;
        return result;
    }
    else if (start_was_pressed) {
        bool show_dialog = false;

        if (PAD_isPressed(BTN_START)) {
            // Check for long press threshold while button is held
            uint32_t hold_time = SDL_GetTicks() - start_press_time;
            if (hold_time >= START_LONG_PRESS_MS) {
                LOG_trace("dialog enter: quit_confirm");
                show_quit_confirm = true;
                show_dialog = true;
            }
        } else if (PAD_justReleased(BTN_START)) {
            // Short press - show controls help
            LOG_trace("dialog enter: controls_help");
            show_controls_help = true;
            show_dialog = true;
        }

        if (show_dialog) {
            start_was_pressed = false;
            // Clear all GPU layers so dialog is not obscured
            GFX_clearLayers(LAYER_SCROLLTEXT);
            PLAT_clearLayers(LAYER_SPECTRUM);
            PLAT_clearLayers(LAYER_PLAYTIME);
            PLAT_GPU_Flip();
            PlayTime_clear();
            result.input_consumed = true;
            result.dirty = true;
            return result;
        }

        // Still waiting for press/release
        result.input_consumed = true;
        return result;
    }

    // Handle power management
    {
        int dirty_before = result.dirty ? 1 : 0;
        int dirty_tmp = dirty_before;
        // PWR_update can block for the full deep-sleep duration if the user
        // presses the power button. Muzzle the watchdog around the call —
        // Watchdog_resume() also refreshes the heartbeat so we don't immediately
        // trip on a stale tick after wake.
        //
        // trace=false: this wrap fires every frame (~60/s) and the operation is
        // almost always a fast no-op. Logging every iteration would drown the
        // ring buffer. Real deep-sleep events still show up in the log as a
        // visible jump in timestamps between consecutive lines.
        Watchdog_pause(WATCHDOG_REASON_DEEP_SLEEP, false);
        PWR_update(&dirty_tmp, show_setting, NULL, NULL);
        Watchdog_resume(WATCHDOG_REASON_DEEP_SLEEP, false);

        if (dirty_tmp && !dirty_before) {
            result.dirty = true;
        }
    }

    return result;
}

void ModuleCommon_setAutosleepDisabled(bool disabled) {
    if (disabled && !autosleep_disabled) {
        PWR_disableAutosleep();
        autosleep_disabled = true;
    } else if (!disabled && autosleep_disabled) {
        // Don't re-enable autosleep if background audio is still playing
        if (!Background_isPlaying()) {
            PWR_enableAutosleep();
            autosleep_disabled = false;
        }
    }
}

bool ModuleCommon_isScreenOffHintActive(void) {
    return screen_off_hint_active;
}

void ModuleCommon_startScreenOffHint(void) {
    screen_off_hint_active = true;
    screen_off_hint_start = SDL_GetTicks();
    screen_off_hint_start_wallclock = time(NULL);
}

void ModuleCommon_resetScreenOffHint(void) {
    screen_off_hint_active = false;
}

void ModuleCommon_recordInputTime(void) {
    last_input_time = SDL_GetTicks();
}

bool ModuleCommon_checkAutoScreenOffTimeout(void) {
    if (screen_off_hint_active) return false;
    uint32_t screen_timeout_ms = Settings_getScreenOffTimeout() * 1000;
    if (screen_timeout_ms > 0 && SDL_GetTicks() - last_input_time >= screen_timeout_ms) {
        ModuleCommon_startScreenOffHint();
        return true;
    }
    return false;
}

bool ModuleCommon_processScreenOffHintTimeout(void) {
    if (!screen_off_hint_active) return false;
    uint32_t now = SDL_GetTicks();
    time_t now_wallclock = time(NULL);
    bool timeout_sdl = (now - screen_off_hint_start >= SCREEN_OFF_HINT_DURATION_MS);
    bool timeout_wallclock = (now_wallclock - screen_off_hint_start_wallclock >= (SCREEN_OFF_HINT_DURATION_MS / 1000));
    if (timeout_sdl || timeout_wallclock) {
        screen_off_hint_active = false;
        PLAT_enableBacklight(0);
        return true;
    }
    return false;
}

void ModuleCommon_quit(void) {
    // Ensure autosleep is re-enabled
    if (autosleep_disabled) {
        PWR_enableAutosleep();
        autosleep_disabled = false;
    }

    // Clear all GPU layers
    GFX_clearLayers(LAYER_SCROLLTEXT);
    PLAT_clearLayers(LAYER_SPECTRUM);
    PLAT_clearLayers(LAYER_PLAYTIME);
    PLAT_clearLayers(LAYER_BUFFER);
}

void ModuleCommon_PWR_update(int* dirty, int* show_setting) {
    // Track overlay-triggering buttons for auto-hide (check BEFORE PWR_update)
    // MENU = brightness, SELECT = color temp, PLUS/MINUS = volume
    bool overlay_buttons_active = PAD_isPressed(BTN_PLUS) || PAD_isPressed(BTN_MINUS)
                               || PAD_isPressed(BTN_MENU) || PAD_isPressed(BTN_SELECT);

    if (overlay_buttons_were_active && !overlay_buttons_active) {
        // Buttons just released - start timer
        overlay_release_time = SDL_GetTicks();
    }

    // Call platform PWR_update. Can block for deep-sleep duration.
    // trace=false for the same reason as the other PWR_update wrap above:
    // this fires every frame, logging would swamp the ring.
    Watchdog_pause(WATCHDOG_REASON_DEEP_SLEEP, false);
    PWR_update(dirty, show_setting, NULL, NULL);
    Watchdog_resume(WATCHDOG_REASON_DEEP_SLEEP, false);

    // After visible period, force hide overlay
    if (overlay_release_time > 0) {
        uint32_t elapsed = SDL_GetTicks() - overlay_release_time;
        if (elapsed >= OVERLAY_VISIBLE_AFTER_RELEASE_MS) {
            // Visible period passed, now force hide
            *show_setting = 0;
            *dirty = 1;
            // Stop forcing after the duration
            if (elapsed >= OVERLAY_VISIBLE_AFTER_RELEASE_MS + OVERLAY_FORCE_HIDE_DURATION_MS) {
                overlay_release_time = 0;
            }
        }
    }

    overlay_buttons_were_active = overlay_buttons_active;
}

// Short label per BTN_ID for the per-frame trace log.
static const char* btn_label(int id) {
    switch (id) {
        case BTN_ID_DPAD_UP:    return "UP";
        case BTN_ID_DPAD_DOWN:  return "DOWN";
        case BTN_ID_DPAD_LEFT:  return "LEFT";
        case BTN_ID_DPAD_RIGHT: return "RIGHT";
        case BTN_ID_A:          return "A";
        case BTN_ID_B:          return "B";
        case BTN_ID_X:          return "X";
        case BTN_ID_Y:          return "Y";
        case BTN_ID_START:      return "START";
        case BTN_ID_SELECT:     return "SELECT";
        case BTN_ID_L1:         return "L1";
        case BTN_ID_R1:         return "R1";
        case BTN_ID_L2:         return "L2";
        case BTN_ID_R2:         return "R2";
        case BTN_ID_L3:         return "L3";
        case BTN_ID_R3:         return "R3";
        case BTN_ID_MENU:       return "MENU";
        case BTN_ID_PLUS:       return "PLUS";
        case BTN_ID_MINUS:      return "MINUS";
        case BTN_ID_POWER:      return "POWER";
        default:                return NULL;
    }
}

void ModuleCommon_traceButtons(void) {
    for (int id = 0; id < BTN_ID_COUNT; id++) {
        const char* label = btn_label(id);
        if (!label) continue;
        uint32_t mask = 1u << id;
        if (PAD_justPressed(mask)) {
            LOG_trace("btn press: %s", label);
            CrashHandler_noteInput(label);
        }
        if (PAD_justReleased(mask)) LOG_trace("btn release: %s", label);
    }
}

#ifdef CRASH_TEST_HOOKS
#include <stdio.h>    // snprintf
#include <stdlib.h>   // abort
#include <unistd.h>   // access, unlink, sleep

// Debug-only crash injection for verifying spec/crash-reporting.md end to end.
// Compiled ONLY with -DCRASH_TEST_HOOKS; a shipping build has none of this.
//
// Triggered by creating a sentinel file:
//
//     touch /tmp/music-player-crash-<kind>          # desktop / host
//     adb shell 'touch /tmp/music-player-crash-<kind>'   # device
//
// where <kind> is a name from crash_triggers[] below. A file rather than a
// button chord so the crash paths can be driven from a script, identically on
// the host desktop build and on the device — and so new kinds are one table row.
//
// Checked in frameBegin(), so it fires from any screen, including mid-playback
// — the interesting case (audio thread live, real track in meta.txt, spectrum
// in screen.bmp).
#define CRASH_TRIGGER_PREFIX  "/tmp/music-player-crash-"

// Must outlast the watchdog threshold (5 s, DEFAULT_THRESHOLD_MS in watchdog.c)
// *plus* the time the handler spends writing the bundle — on device that is a
// ~3 MB screen.bmp to SD, measured at ~3.5 s.
//
// It is not enough to merely trip the watchdog. A real hang never ends, so the
// heartbeat stays stale and meta.txt's heartbeat_age_ms correctly reports the
// stall. A bounded sleep does end: with 7 s the main thread woke and refreshed
// the heartbeat while the handler was still writing the screenshot, and
// format_meta() — which runs last — sampled heartbeat_age_ms as 3 ms instead of
// ~5000, making a genuine watchdog abort look healthy in the bundle. Stay above
// trip + write so the injected hang reads like the real thing.
#define CRASH_TEST_STALL_S    20

// Sentinel checks are access() syscalls and the loop runs at ~60fps, so poll at
// roughly 4 Hz instead of every frame. /tmp is tmpfs on device (no disk I/O),
// but there is no reason for a test hook to cost anything in the hot path.
#define CRASH_TEST_POLL_FRAMES 15

typedef enum {
    CRASH_KIND_SEGV,
    CRASH_KIND_ABORT,
    CRASH_KIND_WATCHDOG,
} CrashTestKind;

// Add new ways to crash here — the filename suffix is the table key.
static const struct {
    const char*   name;
    CrashTestKind kind;
} crash_triggers[] = {
    { "segv",     CRASH_KIND_SEGV     },   // null write  -> SIGSEGV -> bundle, exits
    { "abort",    CRASH_KIND_ABORT    },   // abort()     -> SIGABRT -> bundle, exits
    { "watchdog", CRASH_KIND_WATCHDOG },   // main-loop stall -> watchdog -> SIGABRT
};

// A volatile file-static rather than a local NULL so the compiler cannot prove
// the store is undefined and fold it into a trap instruction — we want a real
// faulting store that the installed SIGSEGV handler sees.
static volatile int* crash_test_null_ptr = NULL;

static void crash_test_fire(CrashTestKind kind) {
    switch (kind) {
        case CRASH_KIND_SEGV:
            LOG_error("CRASH TEST: forcing SIGSEGV via null write\n");
            *crash_test_null_ptr = 1;
            break;
        case CRASH_KIND_ABORT:
            LOG_error("CRASH TEST: forcing abort()\n");
            abort();
            break;
        case CRASH_KIND_WATCHDOG:
            // Blocking here is the point — it is what a real main-thread hang
            // looks like, and the one case an external signal cannot reproduce.
            LOG_error("CRASH TEST: stalling main loop %ds to trip the watchdog\n",
                      CRASH_TEST_STALL_S);
            sleep(CRASH_TEST_STALL_S);
            break;
    }
}

static void crash_test_hooks(void) {
    static int frames = 0;
    if (++frames < CRASH_TEST_POLL_FRAMES) return;
    frames = 0;

    for (size_t i = 0; i < sizeof(crash_triggers) / sizeof(crash_triggers[0]); i++) {
        char path[128];
        snprintf(path, sizeof(path), CRASH_TRIGGER_PREFIX "%s", crash_triggers[i].name);
        if (access(path, F_OK) != 0) continue;

        // Unlink BEFORE crashing. The sentinel outlives the process, so leaving
        // it in place would re-trigger on the next launch and wedge the app in a
        // crash loop that is awkward to break on a handheld.
        unlink(path);
        crash_test_fire(crash_triggers[i].kind);
        return;   // only the watchdog kind returns; the others never get here
    }
}
#endif

void ModuleCommon_frameBegin(void) {
    GFX_startFrame();
    PAD_poll();
    Watchdog_heartbeat();
    ModuleCommon_traceButtons();
#ifdef CRASH_TEST_HOOKS
    crash_test_hooks();
#endif
}

bool ModuleCommon_handleHIDVolume(USBHIDEvent hid_event) {
    if (hid_event != USB_HID_EVENT_VOLUME_UP && hid_event != USB_HID_EVENT_VOLUME_DOWN) {
        return false;
    }
    int vol = GetVolume();
    if (hid_event == USB_HID_EVENT_VOLUME_UP) {
        vol = (vol < 20) ? vol + 1 : 20;
    } else {
        vol = (vol > 0) ? vol - 1 : 0;
    }
    // USB HID events only come from USB DAC, so always use software volume
    SetVolume(vol);
    float v = vol / 20.0f;
    Player_setVolume(v * v * v);
    return true;
}

void ModuleCommon_handleHardwareVolume(void) {
    if (PAD_isPressed(BTN_MENU) || PAD_isPressed(BTN_SELECT)) return;
    if (!PAD_justRepeated(BTN_PLUS) && !PAD_justRepeated(BTN_MINUS)) return;

    // Don't increment volume here - keymon already handles SetVolume().
    // We only need to sync software volume for BT/USB DAC output.
    if (Player_isBluetoothActive() || Player_isUSBDACActive()) {
        int vol = GetVolume();
        float v = vol / 20.0f;
        Player_setVolume(v * v * v);
    }
}
