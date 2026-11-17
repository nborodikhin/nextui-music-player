#include "module_music_playing.h"

#include "defines.h"
#include "api.h"
#include "add_to_playlist.h"
#include "album_art.h"
#include "help_screen.h"
#include "lyrics.h"
#include "music_session.h"
#include "player.h"
#include "settings.h"
#include "spectrum.h"
#include "ui_album_art.h"
#include "ui_main.h"
#include "ui_music_playing.h"
#include "ui_utils.h"

// The seek of a short press of LEFT or RIGHT
#define MUSIC_SEEK_STEP_MS 5000

static bool screen_off = false;

// A display recreate drops every GPU layer. The next frame paints each one
// again, whatever the caches of the screen say.
static void display_recreated(void) {
    MusicPlaying_invalidate();
    // A new display is on and has no content
    screen_off = false;
}

// Handle USB/Bluetooth media button events
static void handle_hid_events(void) {
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
            Player_togglePause();
        } else if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
            MusicSession_next();
        } else if (hid_event == USB_HID_EVENT_PREV_TRACK) {
            MusicSession_prev();
        } else {
            ModuleCommon_handleHIDVolume(hid_event);
        }
    }
}

// The layers of the playing screen, once for each frame of the loop. A frame
// that redraws the screen paints them after that redraw, thus this one leaves
// them to the render block.
static void refresh_gpu_layers(int* dirty) {
    if (ModuleCommon_isScreenOffHintActive()) return;
    if (MusicPlaying_frame(*dirty != 0)) *dirty = 1;
}

static void open_screen(void) {
    DisplayHelper_addRecreatedCallback(display_recreated);
    Spectrum_init();
    screen_off = false;
    ScreenTitle_start(true);
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();
}

// The one exit of the screen, for B, for the end of the queue and for a quit
static ModuleExitReason close_screen(ModuleExitReason reason) {
    if (screen_off) {
        screen_off = false;
        PLAT_enableBacklight(1);
    }
    // The keys lock the playback only
    ModuleCommon_resetScreenOffHint();
    cleanup_album_art_background();
    MusicPlaying_leave();
    Spectrum_quit();
    DisplayHelper_removeRecreatedCallback(display_recreated);
    return reason;
}

ModuleExitReason MusicPlayingModule_run(DisplayContext* display, bool* queue_ended) {
    if (queue_ended) *queue_ended = false;
    if (!display || !MusicSession_isActive()) return MODULE_EXIT_BACK;

    open_screen();
    int dirty = 1;
    int show_setting = 0;
    unsigned shown_serial = MusicSession_getTrackSerial();

    while (1) {
        ModuleCommon_frameBegin(true);
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        if (!MusicSession_isActive()) {
            if (queue_ended) *queue_ended = true;
            return close_screen(MODULE_EXIT_BACK);
        }
        // A track that started in the tick shows its own title, art and position
        if (MusicSession_getTrackSerial() != shown_serial) {
            shown_serial = MusicSession_getTrackSerial();
            // The art of the new track can take the address and the size of the old art
            cleanup_album_art_background();
            dirty = 1;
        }

        // Handle add-to-playlist dialog overlay
        if (AddToPlaylist_isActive()) {
            if (AddToPlaylist_handleInput()) {
                // Dialog closed — skip rest of input to avoid double-handling buttons
                dirty = 1;
                continue;
            }
            // Dialog covers entire screen, no need to render underlying content
            AddToPlaylist_render(screen);
            ModuleCommon_markSurfaceDrawn();
            ModuleCommon_frameEnd(screen);
            continue;
        }

        // Handle global input (skip if screen off or hint active)
        // A signal must reach the global input, thus a pending stop passes this
        // guard. ModuleCommon_handleGlobalInput() reports the quit and returns
        // before it reads any input, thus the screen-off states keep their
        // behavior.
        if (ModuleCommon_stopSignalled()
            || (!screen_off && !ModuleCommon_isScreenOffHintActive())) {
            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, HELP_PLAYER);
            if (global.should_quit) {
                MusicSession_savePosition();
                return close_screen(MODULE_EXIT_QUIT);
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                ModuleCommon_frameEnd(screen);
                continue;
            }
        }

        // Handle screen off hint
        if (ModuleCommon_isScreenOffHintActive()) {
            handle_hid_events();
            if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                ModuleCommon_resetScreenOffHint();
                ModuleCommon_recordInputTime();
                dirty = 1;
                continue;  // skip this frame's input so A doesn't toggle pause
            } else {
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
        }

        // Handle screen off mode
        if (screen_off) {
            if (PAD_anyPressed()) {
                screen_off = false;
                PLAT_enableBacklight(1);
                ModuleCommon_startScreenOffHint();
                GFX_clear(screen);
                render_screen_off_hint(screen);
                ModuleCommon_markSurfaceDrawn();
            }
            handle_hid_events();
            ModuleCommon_frameEnd(screen);
            continue;
        }

        // Normal input handling
        bool key_locked = ModuleCommon_isKeyLocked();
        if (!key_locked && PAD_anyPressed()) {
            ModuleCommon_recordInputTime();
        }

        if (!key_locked && ModuleCommon_updateSeekScan(MUSIC_SEEK_STEP_MS, MUSIC_SEEK_STEP_MS)) {
            dirty = 1;
        }

        if (key_locked) {
            // the screen draws, and the keys do nothing
        }
        else if (PAD_justPressed(BTN_A)) {
            Player_togglePause();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_B)) {
            if (Player_getState() == PLAYER_STATE_PLAYING) {
                MusicSession_savePosition();
            } else {
                MusicSession_stop();
            }
            return close_screen(MODULE_EXIT_BACK);
        }
        else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
            MusicSession_prev();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
            MusicSession_next();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_X)) {
            MusicSession_toggleShuffle();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_Y)) {
            MusicSession_toggleRepeat();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_L3) || PAD_justPressed(BTN_L2)) {
            Spectrum_cycleNext();
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_R3) || PAD_justPressed(BTN_R2)) {
            // Lyrics that are off hide, and keep their data. Lyrics that come back
            // fetch only where the track has none.
            if (Settings_toggleBool(&SETTING_LYRICS_ENABLED)) {
                const TrackInfo* info = Player_getTrackInfo();
                if (info) {
                    Lyrics_fetch(info->artist, info->title, info->duration_ms / 1000,
                                 Player_getCurrentFile(), Player_getEmbeddedLyrics());
                }
            }
            dirty = 1;
        }
        else if (PAD_tappedSelect(SDL_GetTicks())) {
            ModuleCommon_startScreenOffHint();
            if (!ModuleCommon_shouldDrawContentWhileLocked()) MusicPlaying_leave();
            dirty = 1;
        }

        // Re-render when async album art fetch completes
        if (album_art_is_fetching()) dirty = 1;

        // Auto screen-off after inactivity
        if (Player_getState() == PLAYER_STATE_PLAYING && ModuleCommon_checkAutoScreenOffTimeout()) {
            MusicPlaying_leave();
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
                render_playing(screen, show_setting, MusicSession_isShuffle(),
                               MusicSession_isRepeat(), MusicSession_getTrackNumber(),
                               MusicSession_getTrackCount());
                MusicPlaying_paintLayers();
            }

            ModuleCommon_markSurfaceDrawn();
            dirty = 0;
        }
        ModuleCommon_frameEnd(screen);
    }
}
