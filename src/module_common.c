#include <signal.h>
#include <string.h>
#include <time.h>
#include <msettings.h>
#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_player.h"
#include "settings.h"
#include "ui_main.h"
#include "ui_layers.h"
#include "frame_state.h"
#include "ui_utils.h"
#include "player.h"
#include "radio.h"
#include "spectrum.h"
#include "background.h"
#include "toast.h"
#include "test_control.h"
#include "seek_scan.h"
#include "display_helper.h"
#include "pad_reset.h"

static bool autosleep_disabled = false;
static FrameState frame = {0};
static uint32_t last_input_time = 0;

// Screen off hint state
static bool screen_off_hint_active = false;
static uint32_t screen_off_hint_start = 0;
static time_t screen_off_hint_start_wallclock = 0;

// Key lock of a screen that keeps its content, such as on HDMI. The module draws
// as usual and skips its input; a toast shows how to unlock.
static bool key_locked = false;
// SELECT + A was pressed. The lock opens in the next frame, thus the module
// skips that A and does not take it as its own input.
static bool unlock_pending = false;
static ToastToken unlock_toast_token = TOAST_TOKEN_NONE;

// Dialog states
static bool show_quit_confirm = false;

// Set once the user confirms the quit dialog.
// Sticky, any subsequent call to ModuleCommon_handleGlobalInput() will return should_quit = true
static bool quit_requested = false;

// Set by a signal handler, thus sig_atomic_t and not bool. ModuleCommon_init()
// does not clear it: the handlers are in place before the app initializes the
// modules, and a signal in that interval must not be lost.
static volatile sig_atomic_t stop_signalled = 0;
static bool show_controls_help = false;
static bool output_switched = false;

// START button long press detection
static uint32_t start_press_time = 0;
static bool start_was_pressed = false;
#define START_LONG_PRESS_MS 500

// Overlay state tracking - force hide after button release
static bool overlay_buttons_were_active = false;
static uint32_t overlay_release_time = 0;
#define OVERLAY_VISIBLE_AFTER_RELEASE_MS 800  // How long overlay stays visible after release
#define OVERLAY_FORCE_HIDE_DURATION_MS 500    // How long to keep forcing hide

void ModuleCommon_init(void) {
    autosleep_disabled = false;
    last_input_time = SDL_GetTicks();
    screen_off_hint_active = false;
    key_locked = false;
    show_quit_confirm = false;
    quit_requested = false;
    show_controls_help = false;
    start_was_pressed = false;
    overlay_buttons_were_active = false;
    overlay_release_time = 0;
}

// The status pill shows Bluetooth, and no event of the platform redraws it when a
// device connects or goes. Two things do here: a change of the audio sink (at once),
// and a change of the Bluetooth state that the network poll of the platform caches
// (each 5 s). The poll also sees a link that outlives its audio stream, as a headset
// that is switched off does until its supervision timeout.
static bool audio_sink_status_changed(void) {
    static unsigned seen_generation = 0;
    static int seen_bluetooth = -1;
    bool changed = false;

    unsigned generation = Player_getAudioSinkGeneration();
    if (generation != seen_generation) {
        seen_generation = generation;
        changed = true;
    }
    int bluetooth = PLAT_btIsConnected() ? 1 : 0;
    if (bluetooth != seen_bluetooth) {
        if (seen_bluetooth != -1) changed = true;
        seen_bluetooth = bluetooth;
    }
    return changed;
}

static bool woke_from_sleep = false;

// NextUI clears the pad at a wake from sleep
static void on_wake_from_sleep(void) {
    PadReset_record();
    woke_from_sleep = true;
}

bool ModuleCommon_togglePlayback(void) {
    // Radio first: it takes priority while it streams
    RadioState radio_state = Radio_getState();
    PlayerState player_state = Player_getState();

    if (radio_state == RADIO_STATE_PLAYING || radio_state == RADIO_STATE_BUFFERING) {
        Radio_stop();
        return true;
    }
    if (player_state == PLAYER_STATE_PLAYING || player_state == PLAYER_STATE_PAUSED) {
        Player_togglePause();
        return true;
    }
    // Nothing plays: resume the last radio station
    const char* last_url = Radio_getCurrentUrl();
    if (last_url && last_url[0] != '\0') {
        Radio_play(last_url);
        return true;
    }
    return false;
}

bool ModuleCommon_skipTrack(bool next) {
    RadioState radio_state = Radio_getState();

    if (radio_state == RADIO_STATE_PLAYING || radio_state == RADIO_STATE_BUFFERING || radio_state == RADIO_STATE_CONNECTING) {
        // Radio is active - switch stations
        RadioStation* stations;
        int station_count = Radio_getStations(&stations);
        if (station_count <= 1) return false;
        int current_idx = Radio_findCurrentStationIndex();
        if (current_idx < 0) return false;
        int new_idx = next ? (current_idx + 1) % station_count
                           : (current_idx - 1 + station_count) % station_count;
        Radio_stop();
        Radio_play(stations[new_idx].url);
        return true;
    }
    if (PlayerModule_isActive()) {
        if (next) {
            PlayerModule_nextTrack();
        } else {
            PlayerModule_prevTrack();
        }
        return true;
    }
    return false;
}

bool ModuleCommon_handleMediaKey(USBHIDEvent hid_event) {
    switch (hid_event) {
        case USB_HID_EVENT_PLAY_PAUSE:  return ModuleCommon_togglePlayback();
        case USB_HID_EVENT_NEXT_TRACK:  return ModuleCommon_skipTrack(true);
        case USB_HID_EVENT_PREV_TRACK:  return ModuleCommon_skipTrack(false);
        case USB_HID_EVENT_VOLUME_UP:
        case USB_HID_EVENT_VOLUME_DOWN: return ModuleCommon_handleHIDVolume(hid_event);
        case USB_HID_EVENT_NONE:        return false;
    }
    return false;
}

GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, int* show_setting, HelpId help_id) {
    GlobalInputResult result = {false, false, false};

    if (stop_signalled) {
        quit_requested = true;
    }

    // Keep reporting a confirmed quit until somebody acts on it
    if (quit_requested) {
        result.input_consumed = true;
        result.should_quit = true;
        return result;
    }

    if (output_switched) {
        output_switched = false;
        result.input_consumed = true;
        result.dirty = true;
        return result;
    }

    // Poll USB HID events (earphone buttons)
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (ModuleCommon_handleMediaKey(hid_event)) {
            result.dirty = true;
            result.input_consumed = true;
        }
    }

    // The earphone buttons above work while the keys are locked
    if (key_locked) return result;

    // Handle quit confirmation dialog
    if (show_quit_confirm) {
        if (PAD_justPressed(BTN_A)) {
            show_quit_confirm = false;
            quit_requested = true;
            result.input_consumed = true;
            result.should_quit = true;
            return result;
        }
        else if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_START)) {
            show_quit_confirm = false;
            result.input_consumed = true;
            result.dirty = true;
            return result;
        }
        // Dialog is shown, consume input and render (covers entire screen)
        render_confirmation_dialog(screen, NULL, "Quit Music Player?");
        ModuleCommon_markSurfaceDrawn();
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
            show_controls_help = false;
            result.input_consumed = true;
            result.dirty = true;
            return result;
        }
        // Dialog is shown, consume input and render (covers entire screen)
        render_controls_help(screen, help_id);
        ModuleCommon_markSurfaceDrawn();
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
                show_quit_confirm = true;
                show_dialog = true;
            }
        } else if (PAD_justReleased(BTN_START)) {
            // Short press - show controls help, unless the caller already has a
            // dialog up and does not want a second box stacked on it
            if (help_id != HELP_NONE) {
                show_controls_help = true;
                show_dialog = true;
            } else {
                start_was_pressed = false;
            }
        }

        if (show_dialog) {
            start_was_pressed = false;
            // The dialog covers the screen, thus the layers of the screen go.
            // The frame that redraws the screen after the dialog paints them again.
            UiLayer_clear(UI_LAYER_STATUS);
            UiLayer_clear(UI_LAYER_ANIMATION);
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
        PWR_update(&dirty_tmp, show_setting, NULL, on_wake_from_sleep);

        if (dirty_tmp && !dirty_before) {
            result.dirty = true;
        }
    }

    return result;
}

// The work of each frame of a dialog loop that does not depend on the dialog: the track
// advancement and the resume saving, and the earphone buttons
static void tick_dialog_frame(void) {
    Background_tick();
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        ModuleCommon_handleMediaKey(hid_event);
    }
}

// The dialog covers the screen, thus the layers of the screen go
static void clear_screen_layers(void) {
    UiLayer_clear(UI_LAYER_STATUS);
    UiLayer_clear(UI_LAYER_ANIMATION);
}

bool ModuleCommon_runQuitDialog(DisplayContext* display) {
    clear_screen_layers();
    while (!ModuleCommon_isQuitRequested()) {
        ModuleCommon_frameBegin();
        tick_dialog_frame();
        if (PAD_justPressed(BTN_A)) {
            quit_requested = true;
            return true;
        }
        if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_START)) return false;

        SDL_Surface* screen = DisplayHelper_getSurface(display);
        render_confirmation_dialog(screen, NULL, "Quit Music Player?");
        ModuleCommon_markSurfaceDrawn();
        ModuleCommon_frameEnd(screen);
    }
    return true;
}

void ModuleCommon_runControlsHelp(DisplayContext* display, HelpId help_id) {
    clear_screen_layers();
    while (!ModuleCommon_isQuitRequested()) {
        ModuleCommon_frameBegin();
        tick_dialog_frame();
        if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B) || PAD_justPressed(BTN_X) ||
            PAD_justPressed(BTN_Y) || PAD_justPressed(BTN_START) || PAD_justPressed(BTN_SELECT) ||
            PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_DOWN) ||
            PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT) ||
            PAD_justPressed(BTN_L1) || PAD_justPressed(BTN_R1) || PAD_justPressed(BTN_MENU)) {
            return;
        }

        SDL_Surface* screen = DisplayHelper_getSurface(display);
        render_controls_help(screen, help_id);
        ModuleCommon_markSurfaceDrawn();
        ModuleCommon_frameEnd(screen);
    }
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

bool ModuleCommon_shouldDrawContentWhileLocked(void) {
    // HDMI has no backlight to turn off
    return hdmi_active;
}

static void show_unlock_toast(void) {
    unlock_toast_token = Toast_show("Press SELECT + A to unlock", SCREEN_OFF_HINT_DURATION_MS);
}

void ModuleCommon_startScreenOffHint(void) {
    if (ModuleCommon_shouldDrawContentWhileLocked()) {
        key_locked = true;
        show_unlock_toast();
        return;
    }
    screen_off_hint_active = true;
    screen_off_hint_start = SDL_GetTicks();
    screen_off_hint_start_wallclock = time(NULL);
}

void ModuleCommon_resetScreenOffHint(void) {
    screen_off_hint_active = false;
    if (key_locked) {
        key_locked = false;
        unlock_pending = false;
        Toast_dismiss(unlock_toast_token);
    }
}

bool ModuleCommon_isKeyLocked(void) {
    return key_locked;
}

// Runs the key lock of a screen that keeps its content, once for each frame,
// after the poll of the pad.
static void update_key_lock(void) {
    if (unlock_pending) {
        ModuleCommon_resetScreenOffHint();
        ModuleCommon_recordInputTime();
    }
    else if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
        unlock_pending = true;
    }
    else if (PAD_anyJustPressed()) {
        show_unlock_toast();
    }
}

bool ModuleCommon_updateSeekScan(int back_step_ms, int fwd_step_ms) {
    SeekScan* scan = SeekScan_shared();

    // A track that stopped has nothing to seek
    if (Player_getState() == PLAYER_STATE_STOPPED) {
        bool was_scanning = SeekScan_isScanning(scan);
        SeekScan_cancel(scan);
        return was_scanning;
    }

    bool was_scanning = SeekScan_isScanning(scan);
    int seek_to_ms;
    bool seek = SeekScan_update(scan, SDL_GetTicks(), PAD_isPressed(BTN_LEFT),
                                PAD_isPressed(BTN_RIGHT), Player_getPosition(),
                                Player_getDuration(), back_step_ms, fwd_step_ms, &seek_to_ms);
    if (seek) {
        Player_seek(seek_to_ms);
        ModuleCommon_recordInputTime();
    }
    return seek || was_scanning || SeekScan_isScanning(scan);
}

void ModuleCommon_recordInputTime(void) {
    last_input_time = SDL_GetTicks();
}

bool ModuleCommon_checkAutoScreenOffTimeout(void) {
    if (screen_off_hint_active || key_locked) return false;
    uint32_t screen_timeout_ms = Settings_getInt(&SETTING_SCREEN_OFF_TIMEOUT) * 1000;
    if (screen_timeout_ms > 0 && SDL_GetTicks() - last_input_time >= screen_timeout_ms) {
        ModuleCommon_startScreenOffHint();
        return !ModuleCommon_shouldDrawContentWhileLocked();
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

    UiLayer_clear(UI_LAYER_STATUS);
    UiLayer_clear(UI_LAYER_ANIMATION);
}

static void update_power(int* dirty, int* show_setting) {
    // Track overlay-triggering buttons for auto-hide (check BEFORE PWR_update)
    // MENU = brightness, SELECT = color temp, PLUS/MINUS = volume
    bool overlay_buttons_active = PAD_isPressed(BTN_PLUS) || PAD_isPressed(BTN_MINUS)
                               || PAD_isPressed(BTN_MENU) || PAD_isPressed(BTN_SELECT);

    if (overlay_buttons_were_active && !overlay_buttons_active) {
        // Buttons just released - start timer
        overlay_release_time = SDL_GetTicks();
    }
    else if (overlay_buttons_active) {
        // A press ends the release timer, thus a hold that follows a release
        // keeps its pill. The platform shows the pill on each auto-repeat.
        overlay_release_time = 0;
    }

    // Call platform PWR_update
    PWR_update(dirty, show_setting, NULL, on_wake_from_sleep);
    if (audio_sink_status_changed()) *dirty = 1;

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

void ModuleCommon_PWR_update(int* dirty, int* show_setting) {
    update_power(dirty, show_setting);
}

void ModuleCommon_PWR_updateWithWake(int* dirty, int* show_setting, bool* woke) {
    woke_from_sleep = false;
    update_power(dirty, show_setting);
    *woke = woke_from_sleep;
}

void ModuleCommon_requestQuit(void) {
    quit_requested = true;
}

void ModuleCommon_signalStop(void) {
    stop_signalled = 1;
}

bool ModuleCommon_stopSignalled(void) {
    return stop_signalled != 0;
}

bool ModuleCommon_isQuitRequested(void) {
    return quit_requested || stop_signalled != 0;
}

void ModuleCommon_frameBegin(void) {
    if (DisplayHelper_outputChanged()) {
        DisplayHelper_moveToOutput();
        output_switched = true;
        // A cable change is an action of the user, as a key press is. It opens
        // the lock, and the new output starts its own timeout.
        ModuleCommon_resetScreenOffHint();
        ModuleCommon_recordInputTime();
    }
    GFX_startFrame();
    ScreenTitle_frameBegin();
    PAD_poll();
    TestControl_tick();
    if (key_locked) update_key_lock();
    Toast_tick();
}

bool ModuleCommon_takeOutputSwitched(void) {
    bool switched   = output_switched;
    output_switched = false;
    return switched;
}

void ModuleCommon_markSurfaceDrawn(void) {
    FrameState_markSurfaceDrawn(&frame);
}

void ModuleCommon_markLayerDrawn(void) {
    FrameState_markLayerDrawn(&frame);
}

void ModuleCommon_frameEnd(SDL_Surface* screen) {
    // A new audio sink (Bluetooth, USB DAC) moves the audio on the main thread
    Player_handleAudioSinkChange();

    switch (FrameState_take(&frame)) {
        case FRAME_CHANGED_SURFACE: GFX_flip(screen);  break;
        case FRAME_CHANGED_LAYERS:  PLAT_GPU_Flip();   break;
        case FRAME_CHANGED_NOTHING:                    break;
    }
    // A present that the platform does not hold to the display rate, such as
    // one with no window, would let the loop run free
    GFX_sync();
}

void ModuleCommon_changeVolume(int step) {
    int vol = GetVolume() + step;
    if (vol < 0) vol = 0;
    if (vol > 20) vol = 20;
    // keymon does not see these buttons: set the system volume, which NextUI puts on
    // the mixer of the output
    SetVolume(vol);
}

bool ModuleCommon_handleHIDVolume(USBHIDEvent hid_event) {
    if (hid_event != USB_HID_EVENT_VOLUME_UP && hid_event != USB_HID_EVENT_VOLUME_DOWN) {
        return false;
    }
    ModuleCommon_changeVolume(hid_event == USB_HID_EVENT_VOLUME_UP ? 1 : -1);
    return true;
}
