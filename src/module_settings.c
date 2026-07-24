#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_settings.h"
#include "ui_main.h"
#include "settings.h"
#include "selfupdate.h"
#include "downloader.h"
#include "ui_settings.h"
#include "ui_utils.h"
#include "ui_system.h"
#include "wifi.h"
#include "album_art.h"
#include "watchdog.h"
#include "log_trace.h"
#include "crash_handler.h"

// Internal states
typedef enum {
    SETTINGS_STATE_MENU,
    SETTINGS_STATE_CLEAR_CACHE_CONFIRM,
    SETTINGS_STATE_DELETE_CRASH_CONFIRM,
    SETTINGS_STATE_ABOUT,
    SETTINGS_STATE_UPDATING,
    SETTINGS_STATE_UPDATING_YTDLP
} SettingsState;

// Settings menu item IDs and the visible-list builder live in ui_settings.h.

// Internal app state constants for controls help
// These match the pattern used in ui_main.c
#define SETTINGS_INTERNAL_MENU      40
#define SETTINGS_INTERNAL_ABOUT     41

// SD-relative path shown in the "Delete Crash Reports?" confirmation body.
// Refreshed each time we open the dialog.
static char delete_crash_path_buf[128];

ModuleExitReason SettingsModule_run(SDL_Surface* screen) {
    LOG_trace("SettingsModule_run: enter");
    SettingsState state = SETTINGS_STATE_MENU;
    int menu_selected = 0;
    int menu_scroll = 0;
    int dirty = 1;
    int show_setting = 0;

    // Scan the SD card for crash bundles ONCE per settings entry, not per frame.
    // CrashHandler_hasAnyBundle() is an opendir + per-entry stat; the settings
    // loop runs ~60x/s and both the input loop and render need the answer, so a
    // per-call scan was ~120 filesystem sweeps a second on slow storage. The set
    // can only shrink from here (via Delete Crash Reports), which we track below.
    bool has_crash_bundles = CrashHandler_hasAnyBundle();

    while (1) {
        GFX_startFrame();
        PAD_poll();
        Watchdog_heartbeat();
        ModuleCommon_traceButtons();

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

        // Rebuild visible-item list each frame from the cached bundle flag so it
        // tracks runtime state (the Delete Crash Reports row disappears the
        // moment the user confirms a wipe) without per-frame SD I/O.
        int visible_items[SETTINGS_VISIBLE_MAX];
        int visible_count = settings_build_visible_items(visible_items, SETTINGS_VISIBLE_MAX,
                                                         has_crash_bundles);
        if (menu_selected >= visible_count) menu_selected = visible_count - 1;
        if (menu_selected < 0) menu_selected = 0;
        int selected_id = visible_items[menu_selected];

        // State-specific handling
        switch (state) {
            case SETTINGS_STATE_MENU:
                // Navigation
                if (PAD_justPressed(BTN_UP)) {
                    menu_selected = (menu_selected > 0) ? menu_selected - 1 : visible_count - 1;
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_DOWN)) {
                    menu_selected = (menu_selected < visible_count - 1) ? menu_selected + 1 : 0;
                    dirty = 1;
                }
                // Left/Right for cyclable settings; page navigation for others
                else if (PAD_justPressed(BTN_LEFT)) {
                    if (selected_id == SETTINGS_ITEM_SCREEN_OFF) {
                        Settings_cycleScreenOffPrev();
                        dirty = 1;
                    } else if (selected_id == SETTINGS_ITEM_BASS_FILTER) {
                        Settings_cycleBassFilterPrev();
                        dirty = 1;
                    } else if (selected_id == SETTINGS_ITEM_SOFT_LIMITER) {
                        Settings_cycleSoftLimiterPrev();
                        dirty = 1;
                    } else if (selected_id == SETTINGS_ITEM_COLLECT_CRASH) {
                        Settings_toggleCollectCrashReports();
                        dirty = 1;
                    } else {
                        int items_per_page = calc_list_layout(screen).items_per_page;
                        list_page_up(&menu_selected, &menu_scroll, visible_count, items_per_page);
                        dirty = 1;
                    }
                }
                else if (PAD_justPressed(BTN_RIGHT)) {
                    if (selected_id == SETTINGS_ITEM_SCREEN_OFF) {
                        Settings_cycleScreenOffNext();
                        dirty = 1;
                    } else if (selected_id == SETTINGS_ITEM_BASS_FILTER) {
                        Settings_cycleBassFilterNext();
                        dirty = 1;
                    } else if (selected_id == SETTINGS_ITEM_SOFT_LIMITER) {
                        Settings_cycleSoftLimiterNext();
                        dirty = 1;
                    } else if (selected_id == SETTINGS_ITEM_COLLECT_CRASH) {
                        Settings_toggleCollectCrashReports();
                        dirty = 1;
                    } else {
                        int items_per_page = calc_list_layout(screen).items_per_page;
                        list_page_down(&menu_selected, &menu_scroll, visible_count, items_per_page);
                        dirty = 1;
                    }
                }
                // A button
                else if (PAD_justPressed(BTN_A)) {
                    switch (selected_id) {
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
                        case SETTINGS_ITEM_COLLECT_CRASH:
                            Settings_toggleCollectCrashReports();
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_DELETE_CRASH: {
                            LOG_trace("dialog enter: delete_crash_confirm");
                            char raw[96];
                            CrashHandler_getBundleRootDisplayPath(raw, sizeof(raw));
                            // Split the path at the last '/' so it fits on two lines
                            // inside the confirm dialog. The leaf line keeps its
                            // leading '/' and gets a trailing '/' to mark it as the
                            // directory whose contents are about to be removed.
                            char* slash = strrchr(raw, '/');
                            if (slash && slash != raw) {
                                *slash = '\0';
                                snprintf(delete_crash_path_buf, sizeof(delete_crash_path_buf),
                                         "%s\n/%s/", raw, slash + 1);
                            } else {
                                snprintf(delete_crash_path_buf, sizeof(delete_crash_path_buf),
                                         "%s", raw);
                            }
                            state = SETTINGS_STATE_DELETE_CRASH_CONFIRM;
                            dirty = 1;
                            break;
                        }
                        case SETTINGS_ITEM_CLEAR_CACHE:
                            LOG_trace("dialog enter: clear_cache_confirm");
                            state = SETTINGS_STATE_CLEAR_CACHE_CONFIRM;
                            dirty = 1;
                            break;
                        case SETTINGS_ITEM_UPDATE_YTDLP:
                            if (Downloader_init() == 0 && Wifi_ensureConnected(screen, show_setting)) {
                                Downloader_startUpdate();
                                state = SETTINGS_STATE_UPDATING_YTDLP;
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

            case SETTINGS_STATE_CLEAR_CACHE_CONFIRM:
                if (PAD_justPressed(BTN_A)) {
                    // Confirm - clear the cache
                    LOG_trace("dialog exit: clear_cache_confirm action=clear");
                    album_art_clear_disk_cache();
                    state = SETTINGS_STATE_MENU;
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_B)) {
                    // Cancel
                    LOG_trace("dialog exit: clear_cache_confirm action=cancel");
                    state = SETTINGS_STATE_MENU;
                    dirty = 1;
                }
                break;

            case SETTINGS_STATE_DELETE_CRASH_CONFIRM:
                if (PAD_justPressed(BTN_A)) {
                    LOG_trace("dialog exit: delete_crash_confirm action=delete");
                    CrashHandler_deleteAllBundles();
                    has_crash_bundles = false;   // row goes away without a rescan
                    state = SETTINGS_STATE_MENU;
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_B)) {
                    LOG_trace("dialog exit: delete_crash_confirm action=cancel");
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
                        // Quit to apply update
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

            case SETTINGS_STATE_UPDATING_YTDLP:
                Downloader_update();
                const DownloaderUpdateStatus* ytdlp_status = Downloader_getUpdateStatus();

                if (!ytdlp_status->updating) {
                    state = SETTINGS_STATE_MENU;
                }

                if (PAD_justPressed(BTN_B)) {
                    if (ytdlp_status->updating) {
                        Downloader_cancelUpdate();
                    }
                    state = SETTINGS_STATE_MENU;
                }

                dirty = 1;
                break;
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Adjust scroll so menu_selected stays visible
        {
            int items_per_page = calc_list_layout(screen).items_per_page;
            adjust_list_scroll(menu_selected, &menu_scroll, items_per_page);
        }

        // Render
        if (dirty) {
            switch (state) {
                case SETTINGS_STATE_MENU:
                    render_settings_menu(screen, show_setting, menu_selected, menu_scroll, has_crash_bundles);
                    break;
                case SETTINGS_STATE_CLEAR_CACHE_CONFIRM:
                    render_settings_menu(screen, show_setting, menu_selected, menu_scroll, has_crash_bundles);
                    render_confirmation_dialog(screen, NULL, "Clear album art cache?");
                    break;
                case SETTINGS_STATE_DELETE_CRASH_CONFIRM:
                    render_settings_menu(screen, show_setting, menu_selected, menu_scroll, has_crash_bundles);
                    render_confirmation_dialog(screen, delete_crash_path_buf,
                                               "Delete crash reports?");
                    break;
                case SETTINGS_STATE_ABOUT:
                    render_about(screen, show_setting);
                    break;
                case SETTINGS_STATE_UPDATING:
                    render_app_updating(screen, show_setting);
                    break;
                case SETTINGS_STATE_UPDATING_YTDLP:
                    render_ytdlp_updating(screen, show_setting);
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
