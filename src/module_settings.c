#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_settings.h"
#include "display_helper.h"
#include "ui_main.h"
#include "settings.h"
#include "selfupdate.h"
#include "downloader.h"
#include "ui_settings.h"
#include "module_downloader.h"
#include "ui_utils.h"
#include "ui_system.h"
#include "wifi.h"
#include "album_art.h"
#include "list_nav.h"
#include "list_nav_pad.h"

// Internal states
typedef enum {
    SETTINGS_STATE_MENU,
    SETTINGS_STATE_CLEAR_CACHE_CONFIRM,
    SETTINGS_STATE_ABOUT,
    SETTINGS_STATE_UPDATING
} SettingsState;

// Internal app state constants for controls help
// These match the pattern used in ui_main.c
#define SETTINGS_INTERNAL_MENU      40
#define SETTINGS_INTERNAL_ABOUT     41

ModuleExitReason SettingsModule_run(DisplayContext* display) {
    SettingsState state = SETTINGS_STATE_MENU;
    ListNav nav = {
        .selected           = 0,
        .scroll             = 0,
        .count              = SETTINGS_ITEM_COUNT,
        .items_per_page     = 1,
    };
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // Handle global input first
        int app_state = (state == SETTINGS_STATE_MENU) ? SETTINGS_INTERNAL_MENU : SETTINGS_INTERNAL_ABOUT;
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state);
        if (global.should_quit) {
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        // State-specific handling
        switch (state) {
            case SETTINGS_STATE_MENU: {
                nav.items_per_page = calc_list_layout(screen).items_per_page;
                ListNavInput in = ListNavPad_read();

                // Left/Right is overloaded here: on a cyclable row it changes
                // the value, everywhere else it pages.
                //
                // Vertical input wins, so a frame with both bits set navigates
                // only - Up+Right must not cycle the value AND move.
                bool buttonHandledByItem = false;

                bool upDown = (in & (LIST_NAV_UP | LIST_NAV_DOWN)) != 0;
                bool leftRight = (in & (LIST_NAV_LEFT | LIST_NAV_RIGHT)) != 0;
                if (leftRight && !upDown) {
                    bool cycleNext = (in & LIST_NAV_RIGHT) != 0;
                    switch (nav.selected) {
                        case SETTINGS_ITEM_SCREEN_OFF:
                            cycleNext ? Settings_cycleScreenOffNext() : Settings_cycleScreenOffPrev();
                            buttonHandledByItem = true;
                            break;
                        case SETTINGS_ITEM_BASS_FILTER:
                            cycleNext ? Settings_cycleBassFilterNext() : Settings_cycleBassFilterPrev();
                            buttonHandledByItem = true;
                            break;
                        case SETTINGS_ITEM_SOFT_LIMITER:
                            cycleNext ? Settings_cycleSoftLimiterNext() : Settings_cycleSoftLimiterPrev();
                            buttonHandledByItem = true;
                            break;
                        case SETTINGS_ITEM_AUTO_UPDATE:
                            Settings_toggleAutoUpdate();
                            buttonHandledByItem = true;
                            break;
                        default:
                            break;
                    }
                }

                if (buttonHandledByItem) {
                    dirty = 1;
                } else if (ListNav_step(&nav, in).moved) {
                    dirty = 1;
                }
                // A button. Skipped on a frame that already cycled a value, so
                // Right+A does one thing rather than advancing twice.
                else if (PAD_justPressed(BTN_A)) {
                    switch (nav.selected) {
                        case SETTINGS_ITEM_SCREEN_OFF:
                            // A also cycles the value (convenience)
                            Settings_cycleScreenOffNext();
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_BASS_FILTER:
                            Settings_cycleBassFilterNext();
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_SOFT_LIMITER:
                            Settings_cycleSoftLimiterNext();
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_AUTO_UPDATE:
                            Settings_toggleAutoUpdate();
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_CLEAR_CACHE:
                            state = SETTINGS_STATE_CLEAR_CACHE_CONFIRM;
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_UPDATE_YTDLP:
                            // Also the install path: the binary ships separately
                            Downloader_init();
                            if (Wifi_ensureConnected(screen, show_setting) &&
                                DownloaderModule_runInstall(display, &show_setting) == YTDLP_INSTALL_QUIT) {
                                return MODULE_EXIT_QUIT;
                            }
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_ABOUT:
                            state = SETTINGS_STATE_ABOUT;
                            dirty = 1;
                            break;
                    }
                }
                // B button - back to main menu
                else if (PAD_justPressed(BTN_B)) {
                    return MODULE_EXIT_TO_MENU;
                }
                break;
            }

            case SETTINGS_STATE_CLEAR_CACHE_CONFIRM:
                if (PAD_justPressed(BTN_A)) {
                    // Confirm - clear the cache
                    album_art_clear_disk_cache();
                    state = SETTINGS_STATE_MENU;
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_B)) {
                    // Cancel
                    state = SETTINGS_STATE_MENU;
                    dirty = 1;
                }
                break;

            case SETTINGS_STATE_ABOUT:
                SelfUpdate_update();
                const SelfUpdateStatus* status = SelfUpdate_getStatus();

                // Keep refreshing while checking for updates
                if (status->state == SELFUPDATE_STATE_CHECKING) {
                    dirty = 1;
                }

                if (PAD_justPressed(BTN_A)) {
                    if (status->update_available) {
                        SelfUpdate_startUpdate();
                        state = SETTINGS_STATE_UPDATING;
                        dirty = 1;
                    } else if (status->state != SELFUPDATE_STATE_CHECKING) {
                        if (Wifi_ensureConnected(screen, show_setting)) {
                            SelfUpdate_checkForUpdate();
                        }
                        dirty = 1;
                    }
                }
                else if (PAD_justPressed(BTN_B)) {
                    state = SETTINGS_STATE_MENU;
                    dirty = 1;
                }
                break;

            case SETTINGS_STATE_UPDATING:
                // Disable autosleep during update
                ModuleCommon_setAutosleepDisabled(true);

                SelfUpdate_update();
                const SelfUpdateStatus* update_status = SelfUpdate_getStatus();
                SelfUpdateState update_state = update_status->state;

                if (update_state == SELFUPDATE_STATE_COMPLETED) {
                    if (PAD_justPressed(BTN_A)) {
                        // The new binary is already on disk; exiting with the
                        // flag set lets launch.sh bring it straight back up
                        SelfUpdate_requestRestart();
                        ModuleCommon_setAutosleepDisabled(false);
                        return MODULE_EXIT_QUIT;
                    }
                }
                else if (PAD_justPressed(BTN_B)) {
                    if (update_state == SELFUPDATE_STATE_DOWNLOADING) {
                        SelfUpdate_cancelUpdate();
                    }
                    ModuleCommon_setAutosleepDisabled(false);
                    state = SETTINGS_STATE_ABOUT;
                    dirty = 1;
                }

                // Always redraw during update
                dirty = 1;
                break;
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            switch (state) {
                case SETTINGS_STATE_MENU:
                    render_settings_menu(screen, show_setting, nav.selected, nav.scroll);
                    break;
                case SETTINGS_STATE_CLEAR_CACHE_CONFIRM:
                    render_settings_menu(screen, show_setting, nav.selected, nav.scroll);
                    render_confirmation_dialog(screen, NULL, "Clear album art cache?");
                    break;
                case SETTINGS_STATE_ABOUT:
                    render_about(screen, show_setting);
                    break;
                case SETTINGS_STATE_UPDATING:
                    render_app_updating(screen, show_setting);
                    break;
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;
        } else {
            GFX_sync();
        }
    }
}
