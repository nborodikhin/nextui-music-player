#include "module_radio_manage.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "defines.h"
#include "api.h"
#include "help_screen.h"
#include "list_nav.h"
#include "radio.h"
#include "radio_curated.h"
#include "toast.h"
#include "ui_layers.h"
#include "ui_main.h"
#include "ui_radio.h"
#include "ui_utils.h"

typedef enum {
    MANAGE_STATE_COUNTRIES,
    MANAGE_STATE_STATIONS,
    MANAGE_STATE_HELP,
} ManageState;

static ListNav country_nav = {
    .selected       = 0,
    .scroll         = 0,
    .count          = 0,
    .items_per_page = 1,
};
static ListNav station_nav = {
    .selected       = 0,
    .scroll         = 0,
    .count          = 0,
    .items_per_page = 1,
};
static const char* selected_country_code = NULL;
static int help_scroll = 0;
static ManageState help_return_state = MANAGE_STATE_COUNTRIES;

// The remove confirmation of a station of a country
static bool show_confirm = false;
static char confirm_station_name[RADIO_MAX_NAME] = "";
static char confirm_station_url[RADIO_MAX_URL] = "";

// Sorted station index mapping for alphabetical display
static int sorted_station_indices[256];
static int sorted_station_count = 0;

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

static HelpId get_help_id(ManageState state) {
    switch (state) {
        case MANAGE_STATE_COUNTRIES: return HELP_RADIO_MANAGE;
        case MANAGE_STATE_STATIONS:  return HELP_RADIO_BROWSE;
        case MANAGE_STATE_HELP:      return HELP_RADIO_HELP;
    }
    return HELP_RADIO_MANAGE;
}

// Changes the screen, and clears the moving text of the screen before
static ManageState go_to(ManageState state, int* dirty) {
    radio_list_clear_scroll();
    UiLayer_clear(UI_LAYER_ANIMATION);
    *dirty = 1;
    return state;
}

ModuleExitReason RadioManageModule_run(DisplayContext* display) {
    if (!display) return MODULE_EXIT_BACK;

    ManageState state = MANAGE_STATE_COUNTRIES;
    int dirty = 1;
    int show_setting = 0;

    ListNav_scrollToTop(&country_nav);
    show_confirm = false;
    ScreenTitle_start(false);

    while (1) {
        ModuleCommon_frameBegin(true);
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // A pending stop skips the dialog, thus the global input below reports the quit and the
        // module leaves through the path that it already has.
        if (show_confirm && !ModuleCommon_stopSignalled()) {
            if (PAD_justPressed(BTN_A)) {
                if (Radio_removeStationByUrl(confirm_station_url)) {
                    Radio_saveStations();
                    Toast_show("Removed", TOAST_DURATION);
                }
                show_confirm = false;
                dirty = 1;
                ModuleCommon_frameEnd(screen);
                continue;
            } else if (PAD_justPressed(BTN_B)) {
                show_confirm = false;
                dirty = 1;
                ModuleCommon_frameEnd(screen);
                continue;
            }
            // Render confirmation dialog (covers entire screen)
            render_confirmation_dialog(screen, confirm_station_name, "Remove Station?");
            ModuleCommon_markSurfaceDrawn();
            ModuleCommon_frameEnd(screen);
            continue;
        }

        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, get_help_id(state));
        if (global.should_quit) {
            radio_list_clear_scroll();
            // A station that plays in the background stops, thus the station list quits the radio
            Radio_stop();
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            ModuleCommon_frameEnd(screen);
            continue;
        }

        if (state == MANAGE_STATE_COUNTRIES) {
            int country_count = Radio_getCuratedCountryCount();
            country_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&country_nav, country_count).moved) dirty = 1;

            if (ListNav_step(&country_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && country_count > 0) {
                const CuratedCountry* countries = Radio_getCuratedCountries();
                selected_country_code = countries[country_nav.selected].code;
                ListNav_scrollToTop(&station_nav);
                build_sorted_station_indices(selected_country_code);
                state = go_to(MANAGE_STATE_STATIONS, &dirty);
            }
            else if (PAD_justPressed(BTN_Y)) {
                help_return_state = MANAGE_STATE_COUNTRIES;
                help_scroll = 0;
                state = go_to(MANAGE_STATE_HELP, &dirty);
            }
            else if (PAD_justPressed(BTN_B)) {
                radio_list_clear_scroll();
                UiLayer_clear(UI_LAYER_ANIMATION);
                return MODULE_EXIT_BACK;
            }

            if (radio_list_needs_scroll_refresh()) radio_list_animate_scroll();
            if (radio_list_scroll_needs_render()) dirty = 1;
        }
        else if (state == MANAGE_STATE_STATIONS) {
            int station_count = 0;
            const CuratedStation* stations = Radio_getCuratedStations(selected_country_code, &station_count);
            station_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&station_nav, sorted_station_count).moved) dirty = 1;

            if (ListNav_step(&station_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && sorted_station_count > 0) {
                int actual_idx = sorted_station_indices[station_nav.selected];
                const CuratedStation* station = &stations[actual_idx];
                if (Radio_stationExists(station->url)) {
                    // Already subscribed - confirm removal
                    snprintf(confirm_station_name, sizeof(confirm_station_name), "%s", station->name);
                    snprintf(confirm_station_url, sizeof(confirm_station_url), "%s", station->url);
                    show_confirm = true;
                    dirty = 1;
                } else {
                    // Not subscribed - add instantly
                    if (Radio_addStation(station->name, station->url, station->genre, station->slogan) >= 0) {
                        Radio_saveStations();
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Added: %s", station->name);
                        Toast_show(msg, TOAST_DURATION);
                    } else {
                        Toast_show("Maximum 32 stations reached", TOAST_DURATION);
                    }
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_Y)) {
                help_return_state = MANAGE_STATE_STATIONS;
                help_scroll = 0;
                state = go_to(MANAGE_STATE_HELP, &dirty);
            }
            else if (PAD_justPressed(BTN_B)) {
                state = go_to(MANAGE_STATE_COUNTRIES, &dirty);
            }

            if (radio_list_needs_scroll_refresh()) radio_list_animate_scroll();
            if (radio_list_scroll_needs_render()) dirty = 1;
        }
        else if (state == MANAGE_STATE_HELP) {
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

        ModuleCommon_PWR_update(&dirty, &show_setting);

        if (dirty) {
            switch (state) {
                case MANAGE_STATE_COUNTRIES:
                    render_radio_add(screen, show_setting, country_nav.selected, &country_nav.scroll);
                    break;
                case MANAGE_STATE_STATIONS:
                    render_radio_add_stations(screen, show_setting, selected_country_code,
                                              station_nav.selected, &station_nav.scroll,
                                              sorted_station_indices, sorted_station_count);
                    break;
                case MANAGE_STATE_HELP:
                    render_radio_help(screen, show_setting, &help_scroll);
                    break;
            }
            if (show_setting) GFX_blitHardwareHints(screen, show_setting);

            ModuleCommon_markSurfaceDrawn();
            dirty = 0;
        }
        ScreenTitle_frameEnd(&dirty, true);
        ModuleCommon_frameEnd(screen);
    }
}
