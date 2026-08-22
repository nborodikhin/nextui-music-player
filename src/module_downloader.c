#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "toast.h"
#include "module_downloader.h"
#include "keyboard.h"
#include "display_helper.h"
#include "module_library.h"
#include "downloader.h"
#include "ui_downloader.h"
#include "ui_main.h"
#include "ui_utils.h"
#include "wifi.h"
#include "list_nav.h"
#include "list_nav_pad.h"

// Menu count
#define DOWNLOADER_MENU_COUNT 2

// Controls-help state for the yt-dlp install/update screen
#define DOWNLOADER_YTDLP_HELP_STATE 42

// Internal states
typedef enum {
    DOWNLOADER_INTERNAL_MENU,
    DOWNLOADER_INTERNAL_SEARCHING,
    DOWNLOADER_INTERNAL_RESULTS,
    DOWNLOADER_INTERNAL_QUEUE
} DownloaderInternalState;

// Module state. Three cursors, one per list state.
static ListNav menu_nav = {
    .selected = 0,
    .scroll = 0,
    .count = DOWNLOADER_MENU_COUNT,
    .items_per_page = 1,
};
static ListNav results_nav = {
    .selected = 0,
    .scroll = 0,
    .count = 0,
    .items_per_page = 1,
};
// Id of the item under the queue cursor, remembered across frames: the worker
// removes a completed item from wherever it sat, so a count delta alone cannot
// say how far the selection shifted.
static char queue_cursor_id[DOWNLOADER_VIDEO_ID_LEN] = "";

static ListNav queue_nav = {
    .selected = 0,
    .scroll = 0,
    .count = 0,
    .items_per_page = 1,
};
static DownloaderResult* results = NULL;
static int result_count = 0;

YtdlpInstallResult DownloaderModule_runInstall(DisplayContext* display, int* show_setting) {
    Downloader_startUpdateCheck();

    int dirty = 1;
    YtdlpUiState shown = YTDLP_UI_UNCHECKED;

    // One screen for the whole thing: it checks, then asks, then installs, and
    // the layout only gains a progress bar on the way. A dialog on top would
    // have covered the versions the user is being asked to decide about.
    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, show_setting,
                                                                  DOWNLOADER_YTDLP_HELP_STATE);
        if (global.should_quit) {
            Downloader_cancelUpdate();
            return YTDLP_INSTALL_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        Downloader_update();
        const DownloaderUpdateStatus status = Downloader_getUpdateStatus();
        const YtdlpUiState ui = Downloader_updateUiState(&status);

        // The workers finish on their own threads, so the frame that has to
        // repaint is the one where the state changed under us
        if (ui != shown) {
            shown = ui;
            dirty = 1;
        }

        if (PAD_justPressed(BTN_A)) {
            if (ui == YTDLP_UI_AVAILABLE) {
                // Nothing has been transferred until here
                Downloader_startUpdate();
                dirty = 1;
            } else if (ui != YTDLP_UI_CHECKING && ui != YTDLP_UI_INSTALLING) {
                break;  // a settled result; A dismisses it like B does
            }
        }
        else if (PAD_justPressed(BTN_B)) {
            if (ui == YTDLP_UI_CHECKING || ui == YTDLP_UI_INSTALLING) {
                Downloader_cancelUpdate();
            }
            break;
        }

        ModuleCommon_PWR_update(&dirty, show_setting);

        // Progress advances on its own, so redraw every frame while it runs
        if (dirty || ui == YTDLP_UI_INSTALLING) {
            render_ytdlp_updating(screen, *show_setting);
            if (*show_setting) {
                GFX_blitHardwareHints(screen, *show_setting);
            }
            GFX_flip(screen);
            dirty = 0;
        } else {
            GFX_sync();
        }
    }

    Downloader_refreshVersion();
    return YTDLP_INSTALL_DONE;
}

// Ask before spending the user's bandwidth on the yt-dlp download.
// Returns true when the user accepts; sets should_quit when they exit the app.

ModuleExitReason DownloaderModule_run(DisplayContext* display) {
    Downloader_init();

    // Check WiFi before entering
    int show_setting = 0;
    if (!Wifi_ensureConnected(DisplayHelper_getSurface(display), show_setting)) {
        Downloader_cleanup();
        Toast_show("Internet connection required", TOAST_DURATION);
        return MODULE_EXIT_TO_MENU;
    }

    // The helpers ship separately from the app - offer to fetch them on first
    // use. The install screen says what it is doing and why, so no toast rides
    // over it, and declining is the user's own decision to be told about.
    if (!Downloader_isAvailable()) {
        if (DownloaderModule_runInstall(display, &show_setting) == YTDLP_INSTALL_QUIT) {
            Downloader_cleanup();
            return MODULE_EXIT_QUIT;
        }
        if (Downloader_init() != 0) {
            Downloader_cleanup();
            return MODULE_EXIT_TO_MENU;
        }
    }

    DownloaderInternalState state = DOWNLOADER_INTERNAL_MENU;
    int dirty = 1;
    char search_query[256] = "";

    ListNav_scrollToTop(&menu_nav);
    ListNav_scrollToTop(&queue_nav);
    ListNav_scrollToTop(&results_nav);
    results = NULL;
    result_count = 0;

    // If re-entering while download is running, go straight to queue
    if (Downloader_isDownloading()) {
        ListNav_scrollToTop(&queue_nav);
        state = DOWNLOADER_INTERNAL_QUEUE;
    }

    while (1) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        // Handle global input
        int app_state_for_help;
        switch (state) {
            case DOWNLOADER_INTERNAL_MENU: app_state_for_help = 28; break;
            case DOWNLOADER_INTERNAL_SEARCHING: app_state_for_help = 29; break;
            case DOWNLOADER_INTERNAL_RESULTS: app_state_for_help = 30; break;
            case DOWNLOADER_INTERNAL_QUEUE: app_state_for_help = 31; break;
        }

        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help);
        if (global.should_quit) {
            Downloader_cleanup();
            return MODULE_EXIT_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        // =========================================
        // MENU STATE
        // =========================================
        if (state == DOWNLOADER_INTERNAL_MENU) {
            menu_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_step(&menu_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                if (menu_nav.selected == 0) {
                    // Search Music
                    char* query = Keyboard_open("Search:");
                    if (query && strlen(query) > 0) {
                        snprintf(search_query, sizeof(search_query), "%s", query);
                        ListNav_scrollToTop(&results_nav);
                        results = NULL;
                        result_count = 0;
                        if (Downloader_startSearch(query) == 0) {
                            state = DOWNLOADER_INTERNAL_SEARCHING;
                        } else {
                            // Search failed to start (likely another search in progress)
                            Toast_show("Search already in progress", TOAST_DURATION);
                        }
                    }
                    if (query) free(query);
                    // The keyboard may have recreated the display - start a fresh frame.
                    dirty = 1;
                    continue;
                } else if (menu_nav.selected == 1) {
                    // Download Queue
                    ListNav_scrollToTop(&queue_nav);
                    state = DOWNLOADER_INTERNAL_QUEUE;
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                if (Downloader_isDownloading()) {
                    // Keep download running in background
                    Downloader_saveQueue();
                } else {
                    Downloader_cleanup();
                }
                return MODULE_EXIT_TO_MENU;
            }
        }
        // =========================================
        // SEARCHING STATE
        // =========================================
        else if (state == DOWNLOADER_INTERNAL_SEARCHING) {
            Downloader_update();
            const DownloaderSearchStatus search_status = Downloader_getSearchStatus();
            if (search_status.completed) {
                if (search_status.result_count > 0) {
                    results = Downloader_getSearchResults();
                    result_count = search_status.result_count;
                    ListNav_reconcile(&results_nav, result_count);
                    ListNav_scrollToTop(&results_nav);
                    state = DOWNLOADER_INTERNAL_RESULTS;
                } else {
                    Toast_show(search_status.error_message[0]
                                   ? search_status.error_message : "No results found",
                               TOAST_DURATION);
                    state = DOWNLOADER_INTERNAL_MENU;
                }
            }

            if (PAD_justPressed(BTN_B)) {
                Downloader_cancelSearch();
                state = DOWNLOADER_INTERNAL_MENU;
            }

            dirty = 1;  // Keep refreshing for spinner
        }
        // =========================================
        // RESULTS STATE
        // =========================================
        else if (state == DOWNLOADER_INTERNAL_RESULTS) {
            results_nav.items_per_page = calc_list_layout(screen).items_per_page;
            if (ListNav_reconcile(&results_nav, result_count).moved) dirty = 1;
            if (ListNav_step(&results_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && result_count > 0) {
                DownloaderResult* r = &results[results_nav.selected];
                if (Downloader_isInQueue(r->video_id)) {
                    Toast_show("Already in queue", TOAST_DURATION);
                } else {
                    int added = Downloader_queueAdd(r->video_id, r->title);
                    if (added == 1) {
                        // queueAdd auto-starts download thread
                        Toast_show(Downloader_isDownloading() ? "Added to queue" : "Downloading...",
                                   TOAST_DURATION);
                    } else if (added == -1) {
                        Toast_show("Queue is full", TOAST_DURATION);
                    }
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                downloader_results_clear_scroll();
                GFX_clearLayers(LAYER_SCROLLTEXT);
                state = DOWNLOADER_INTERNAL_MENU;
                dirty = 1;
            }

            if (downloader_results_needs_scroll_refresh()) {
                downloader_results_animate_scroll();
            }
            if (downloader_results_scroll_needs_render()) dirty = 1;
        }
        // =========================================
        // QUEUE STATE
        // =========================================
        else if (state == DOWNLOADER_INTERNAL_QUEUE) {
            int qcount = 0;
            DownloaderQueueItem* qitems = Downloader_queueGet(&qcount);
            queue_nav.items_per_page = calc_list_layout(screen).rich_items_per_page;

            if (queue_nav.count != qcount) {
                // Find where the selected item went. What vanished from above it
                // is what shifted the selection; moving the window by the same
                // amount holds it on its screen row. Redraw regardless - rows
                // below the cursor repaint even when the cursor does not move.
                int now_at = -1;
                if (queue_cursor_id[0]) {
                    for (int i = 0; i < qcount; i++) {
                        if (strcmp(qitems[i].video_id, queue_cursor_id) == 0) { now_at = i; break; }
                    }
                }
                if (now_at >= 0 && now_at < queue_nav.selected) {
                    ListNav_onItemsRemoved(&queue_nav, 0, queue_nav.selected - now_at);
                }
                ListNav_reconcile(&queue_nav, qcount);
                dirty = 1;
            }

            if (ListNav_step(&queue_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && qcount > 0) {
                // Queue is now a monitoring page — downloads auto-start from search
            }
            else if (PAD_justPressed(BTN_X) && qcount > 0) {
                int removed_index = queue_nav.selected;
                Downloader_queueRemove(removed_index);
                downloader_queue_clear_scroll();
                ListNav_onItemRemoved(&queue_nav, removed_index);
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                downloader_queue_clear_scroll();
                state = DOWNLOADER_INTERNAL_MENU;
                dirty = 1;
            }

            if (downloader_queue_needs_scroll_refresh()) {
                downloader_queue_animate_scroll();
            }
            if (downloader_queue_scroll_needs_render()) dirty = 1;

            // Remember what the cursor is on, for the next count change.
            if (queue_nav.selected >= 0 && queue_nav.selected < qcount) {
                snprintf(queue_cursor_id, sizeof(queue_cursor_id), "%s",
                         qitems[queue_nav.selected].video_id);
            } else {
                queue_cursor_id[0] = '\0';
            }
        }
        // Keep refreshing while download is active (for progress updates)
        if (state == DOWNLOADER_INTERNAL_QUEUE && Downloader_isDownloading()) {
            dirty = 1;
        }
        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            switch (state) {
                case DOWNLOADER_INTERNAL_MENU:
                    render_downloader_menu(screen, show_setting, menu_nav.selected, menu_nav.scroll);
                    break;
                case DOWNLOADER_INTERNAL_SEARCHING:
                    render_downloader_searching(screen, show_setting, search_query);
                    break;
                case DOWNLOADER_INTERNAL_RESULTS:
                    render_downloader_results(screen, show_setting, search_query, results, result_count,
                                              results_nav.selected, &results_nav.scroll, false);
                    break;
                case DOWNLOADER_INTERNAL_QUEUE:
                    render_downloader_queue(screen, show_setting, queue_nav.selected, &queue_nav.scroll);
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
