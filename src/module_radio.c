#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "module_common.h"
#include "toast.h"
#include "module_radio.h"
#include "player.h"
#include "radio.h"
#include "radio_curated.h"
#include "album_art.h"
#include "ui_radio.h"
#include "ui_album_art.h"
#include "ui_main.h"
#include "ui_utils.h"
#include "wifi.h"
#include "list_nav.h"
#include "list_nav_pad.h"
#include "background.h"

// Internal states
typedef enum {
    RADIO_INTERNAL_LIST,
    RADIO_INTERNAL_PLAYING,
    RADIO_INTERNAL_ADD_COUNTRY,
    RADIO_INTERNAL_ADD_STATIONS,
    RADIO_INTERNAL_HELP
} RadioInternalState;

// Module state
static ListNav radio_nav = {
    .selected           = 0,
    .scroll             = 0,
    .count              = 0,
    .items_per_page     = 1,
};
// The "added / limit reached" toast on the add-stations screen, so leaving that
// screen can take it down with it.
static ToastToken add_station_toast = TOAST_TOKEN_NONE;

// Add stations UI state
static ListNav add_country_nav = {
    .selected           = 0,
    .scroll             = 0,
    .count              = 0,
    .items_per_page     = 1,
};
static ListNav add_station_nav = {
    .selected           = 0,
    .scroll             = 0,
    .count              = 0,
    .items_per_page     = 1,
};
static const char* add_selected_country_code = NULL;
static int help_scroll = 0;

// Confirmation dialog state
static bool show_confirm = false;
static int confirm_action_type = 0;   // 0 = delete from main list, 1 = remove from browse
static int confirm_target_index = -1;
static char confirm_station_name[RADIO_MAX_NAME] = "";
static char confirm_station_url[RADIO_MAX_URL] = "";

// Help screen back-navigation
static RadioInternalState help_return_state = RADIO_INTERNAL_ADD_COUNTRY;

// Sorted station index mapping for alphabetical display
static int sorted_station_indices[256];
static int sorted_station_count = 0;

// Screen off state
static bool screen_off = false;

// Last rendered metadata (for change detection)
static char last_rendered_artist[256] = "";
static char last_rendered_title[256] = "";
static bool last_art_was_fetching = false;

// Handle USB/Bluetooth media button events
static void handle_hid_events(void) {
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
            if (Radio_isActive()) {
                Radio_stop();
            } else {
                const char* url = Radio_getCurrentUrl();
                if (url && url[0] != '\0') {
                    Radio_play(url);
                }
            }
        } else if (hid_event == USB_HID_EVENT_NEXT_TRACK || hid_event == USB_HID_EVENT_PREV_TRACK) {
            RadioStation* stations;
            int station_count = Radio_getStations(&stations);
            if (station_count > 1) {
                int current_idx = Radio_findCurrentStationIndex();
                if (current_idx < 0) current_idx = 0;
                int new_idx = (hid_event == USB_HID_EVENT_NEXT_TRACK)
                    ? (current_idx + 1) % station_count
                    : (current_idx - 1 + station_count) % station_count;
                Radio_stop();
                Radio_play(stations[new_idx].url);
            }
        } else {
            ModuleCommon_handleHIDVolume(hid_event);
        }
    }
}

static void build_sorted_station_indices(const char* country_code) {
    int sc = 0;
    const CuratedStation* cs = Radio_getCuratedStations(country_code, &sc);
    sorted_station_count = (sc < 256) ? sc : 256;
    for (int i = 0; i < sorted_station_count; i++) sorted_station_indices[i] = i;
    // Insertion sort by name (max ~50 stations per country, adequate)
    for (int i = 1; i < sorted_station_count; i++) {
        int key = sorted_station_indices[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(cs[sorted_station_indices[j]].name, cs[key].name) > 0) {
            sorted_station_indices[j + 1] = sorted_station_indices[j];
            j--;
        }
        sorted_station_indices[j + 1] = key;
    }
}

ModuleExitReason RadioModule_run(SDL_Surface* screen) {
    Radio_init();

    RadioInternalState state = RADIO_INTERNAL_LIST;
    int dirty = 1;
    int show_setting = 0;

    screen_off = false;
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();
    show_confirm = false;

    // Re-enter playing state if radio is playing in background
    if (Background_getActive() == BG_RADIO && Radio_isActive()) {
        Background_setActive(BG_NONE);
        ModuleCommon_setAutosleepDisabled(true);
        last_rendered_artist[0] = '\0';
        last_rendered_title[0] = '\0';
        last_art_was_fetching = false;
        state = RADIO_INTERNAL_PLAYING;
    }

    while (1) {
        ModuleCommon_frameBegin();

        // Handle confirmation dialog
        if (show_confirm) {
            if (PAD_justPressed(BTN_A)) {
                if (confirm_action_type == 0) {
                    // Delete from main list
                    Radio_removeStation(confirm_target_index);
                    Radio_saveStations();
                    ListNav_onItemRemoved(&radio_nav, confirm_target_index);
                } else if (confirm_action_type == 1) {
                    // Remove from browse
                    Radio_removeStationByUrl(confirm_station_url);
                    Radio_saveStations();
                }
                show_confirm = false;
                dirty = 1;
                GFX_sync();
                continue;
            } else if (PAD_justPressed(BTN_B)) {
                show_confirm = false;
                dirty = 1;
                GFX_sync();
                continue;
            }
            // Render confirmation dialog (covers entire screen)
            render_confirmation_dialog(screen, confirm_station_name, "Remove Station?");
            GFX_flip(screen);
            GFX_sync();
            continue;
        }

        // Handle global input (skip if screen off or hint active)
        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            // Map internal state to app state for controls help context
            int app_state_for_help;
            switch (state) {
                case RADIO_INTERNAL_LIST: app_state_for_help = 3; break;  // STATE_RADIO_LIST
                case RADIO_INTERNAL_PLAYING: app_state_for_help = 4; break;  // STATE_RADIO_PLAYING
                case RADIO_INTERNAL_ADD_COUNTRY: app_state_for_help = 5; break;  // STATE_RADIO_ADD
                case RADIO_INTERNAL_ADD_STATIONS: app_state_for_help = 6; break;  // STATE_RADIO_ADD_STATIONS
                case RADIO_INTERNAL_HELP: app_state_for_help = 7; break;  // STATE_RADIO_HELP
                default: app_state_for_help = 3; break;
            }

            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help);
            if (global.should_quit) {
                Radio_quit();
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                GFX_sync();
                continue;
            }
        }

        // =========================================
        // RADIO LIST STATE
        // =========================================
        if (state == RADIO_INTERNAL_LIST) {
            RadioStation* stations;
            int station_count = Radio_getStations(&stations);
            radio_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&radio_nav, station_count).moved) dirty = 1;

            if (ListNav_step(&radio_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && station_count > 0) {
                if (!Wifi_ensureConnected(screen, show_setting)) {
                    Toast_show("Internet connection required", TOAST_DURATION);
                    dirty = 1;
                } else {
                    Background_stopAll();
                    if (Radio_play(stations[radio_nav.selected].url) == 0) {
                        ModuleCommon_recordInputTime();
                        last_rendered_artist[0] = '\0';
                        last_rendered_title[0] = '\0';
                        last_art_was_fetching = false;
                        state = RADIO_INTERNAL_PLAYING;
                        dirty = 1;
                    }
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                if (!Radio_isActive()) {
                    Radio_quit();
                }
                return MODULE_EXIT_TO_MENU;
            }
            else if (PAD_justPressed(BTN_Y)) {
                ListNav_scrollToTop(&add_country_nav);
                state = RADIO_INTERNAL_ADD_COUNTRY;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && station_count > 0) {
                strncpy(confirm_station_name, stations[radio_nav.selected].name, RADIO_MAX_NAME - 1);
                confirm_station_name[RADIO_MAX_NAME - 1] = '\0';
                confirm_target_index = radio_nav.selected;
                confirm_action_type = 0;
                show_confirm = true;
                dirty = 1;
            }
        }
        // =========================================
        // RADIO PLAYING STATE
        // =========================================
        else if (state == RADIO_INTERNAL_PLAYING) {
            ModuleCommon_setAutosleepDisabled(true);

            // Handle screen off hint
            if (ModuleCommon_isScreenOffHintActive()) {
                handle_hid_events();
                ModuleCommon_handleHardwareVolume();
                Radio_update();

                // SELECT+A during hint -> full wake
                if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                    ModuleCommon_resetScreenOffHint();
                    ModuleCommon_recordInputTime();
                    dirty = 1;
                    continue;
                } else {
                    // Any other button resets the hint timer
                    if (PAD_anyPressed()) {
                        ModuleCommon_startScreenOffHint();
                    }
                    if (ModuleCommon_processScreenOffHintTimeout()) {
                        screen_off = true;
                        GFX_clear(screen);
                        GFX_flip(screen);
                    }
                    GFX_sync();
                    continue;
                }
            }

            // Handle screen off
            if (screen_off) {
                handle_hid_events();
                ModuleCommon_handleHardwareVolume();
                Radio_update();

                // Any button -> show hint
                if (PAD_anyPressed()) {
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    ModuleCommon_startScreenOffHint();
                    GFX_clear(screen);
                    render_screen_off_hint(screen);
                    GFX_flip(screen);
                }
                GFX_sync();
                continue;
            }

            // Reset input timer on any button press
            if (PAD_anyPressed()) {
                ModuleCommon_recordInputTime();
            }

            // Station switching
            RadioStation* stations;
            int station_count = Radio_getStations(&stations);

            if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
                if (station_count > 1) {
                    radio_nav.selected = (radio_nav.selected + 1) % station_count;
                    Radio_stop();
                    Radio_play(stations[radio_nav.selected].url);
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
                if (station_count > 1) {
                    radio_nav.selected = (radio_nav.selected - 1 + station_count) % station_count;
                    Radio_stop();
                    Radio_play(stations[radio_nav.selected].url);
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                cleanup_album_art_background();
                RadioStatus_clear();
                if (Radio_isActive()) {
                    Background_setActive(BG_RADIO);
                } else {
                    ModuleCommon_setAutosleepDisabled(false);
                }
                state = RADIO_INTERNAL_LIST;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                // A toggles play/pause
                if (Radio_isActive()) {
                    // Playing - stop it
                    Radio_stop();
                    dirty = 1;
                } else {
                    // Stopped - resume playing
                    const char* url = Radio_getCurrentUrl();
                    if (url && url[0] != '\0') {
                        Radio_play(url);
                        dirty = 1;
                    }
                }
            }
            else if (PAD_tappedSelect(SDL_GetTicks())) {
                ModuleCommon_startScreenOffHint();
                GFX_clearLayers(LAYER_SCROLLTEXT);
                PLAT_clearLayers(LAYER_BUFFER);
                PLAT_GPU_Flip();
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
                GFX_clearLayers(LAYER_SCROLLTEXT);
                PLAT_clearLayers(LAYER_BUFFER);
                PLAT_GPU_Flip();
                dirty = 1;
            }

            // Animate radio GPU layer
            if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
                if (RadioStatus_needsRefresh()) {
                    RadioStatus_renderGPU();
                }
            }
        }
        // =========================================
        // ADD COUNTRY STATE
        // =========================================
        else if (state == RADIO_INTERNAL_ADD_COUNTRY) {
            int country_count = Radio_getCuratedCountryCount();
            add_country_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&add_country_nav, country_count).moved) dirty = 1;

            if (ListNav_step(&add_country_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && country_count > 0) {
                const CuratedCountry* countries = Radio_getCuratedCountries();
                add_selected_country_code = countries[add_country_nav.selected].code;
                ListNav_scrollToTop(&add_station_nav);
                build_sorted_station_indices(add_selected_country_code);
                state = RADIO_INTERNAL_ADD_STATIONS;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_Y)) {
                help_return_state = RADIO_INTERNAL_ADD_COUNTRY;
                help_scroll = 0;
                state = RADIO_INTERNAL_HELP;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                state = RADIO_INTERNAL_LIST;
                dirty = 1;
            }
        }
        // =========================================
        // ADD STATIONS STATE
        // =========================================
        else if (state == RADIO_INTERNAL_ADD_STATIONS) {
            int station_count = 0;
            const CuratedStation* stations = Radio_getCuratedStations(add_selected_country_code, &station_count);
            add_station_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&add_station_nav, sorted_station_count).moved) dirty = 1;

            if (ListNav_step(&add_station_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && sorted_station_count > 0) {
                int actual_idx = sorted_station_indices[add_station_nav.selected];
                const CuratedStation* station = &stations[actual_idx];
                if (Radio_stationExists(station->url)) {
                    // Already subscribed - confirm removal
                    strncpy(confirm_station_name, station->name, RADIO_MAX_NAME - 1);
                    confirm_station_name[RADIO_MAX_NAME - 1] = '\0';
                    strncpy(confirm_station_url, station->url, RADIO_MAX_URL - 1);
                    confirm_station_url[RADIO_MAX_URL - 1] = '\0';
                    confirm_action_type = 1;
                    show_confirm = true;
                    dirty = 1;
                } else {
                    // Not subscribed - add instantly
                    if (Radio_addStation(station->name, station->url, station->genre, station->slogan) >= 0) {
                        Radio_saveStations();
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Added: %s", station->name);
                        add_station_toast = Toast_show(msg, TOAST_DURATION);
                    } else {
                        add_station_toast = Toast_show("Maximum 32 stations reached", TOAST_DURATION);
                    }
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_Y)) {
                help_return_state = RADIO_INTERNAL_ADD_STATIONS;
                help_scroll = 0;
                state = RADIO_INTERNAL_HELP;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                Toast_dismiss(add_station_toast);
                state = RADIO_INTERNAL_ADD_COUNTRY;
                dirty = 1;
            }
        }
        // =========================================
        // HELP STATE
        // =========================================
        else if (state == RADIO_INTERNAL_HELP) {
            int scroll_step = SCALE1(18);

            if (PAD_justRepeated(BTN_UP)) {
                if (help_scroll > 0) {
                    help_scroll -= scroll_step;
                    if (help_scroll < 0) help_scroll = 0;
                    dirty = 1;
                }
            }
            else if (PAD_justRepeated(BTN_DOWN)) {
                help_scroll += scroll_step;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                help_scroll = 0;
                state = help_return_state;
                dirty = 1;
            }
        }

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
                switch (state) {
                    case RADIO_INTERNAL_LIST:
                        render_radio_list(screen, show_setting, radio_nav.selected, &radio_nav.scroll);
                        break;
                    case RADIO_INTERNAL_PLAYING: {
                        render_radio_playing(screen, show_setting, radio_nav.selected);
                        const RadioMetadata* meta = Radio_getMetadata();
                        strncpy(last_rendered_artist, meta->artist, sizeof(last_rendered_artist) - 1);
                        last_rendered_artist[sizeof(last_rendered_artist) - 1] = '\0';
                        strncpy(last_rendered_title, meta->title, sizeof(last_rendered_title) - 1);
                        last_rendered_title[sizeof(last_rendered_title) - 1] = '\0';
                        break;
                    }
                    case RADIO_INTERNAL_ADD_COUNTRY:
                        render_radio_add(screen, show_setting, add_country_nav.selected, &add_country_nav.scroll);
                        break;
                    case RADIO_INTERNAL_ADD_STATIONS:
                        render_radio_add_stations(screen, show_setting, add_selected_country_code,
                                                  add_station_nav.selected, &add_station_nav.scroll,
                                                  sorted_station_indices, sorted_station_count);
                        break;
                    case RADIO_INTERNAL_HELP:
                        render_radio_help(screen, show_setting, &help_scroll);
                        break;
                }
            }

            if (show_setting && state != RADIO_INTERNAL_PLAYING) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;
        } else if (!screen_off) {
            GFX_sync();
        }
    }
}
