#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defines.h"
#include "api.h"
#include "config.h"
#include "module_common.h"
#include "toast.h"
#include "module_podcast.h"
#include "podcast.h"
#include "player.h"
#include "keyboard.h"
#include "display_helper.h"
#include "ui_podcast.h"
#include "ui_radio.h"
#include "ui_main.h"
#include "ui_utils.h"
#include "wifi.h"
#include "list_nav.h"
#include "list_nav_pad.h"
#include "background.h"

// Internal states
typedef enum {
    PODCAST_INTERNAL_MENU,
    PODCAST_INTERNAL_MANAGE,
    PODCAST_INTERNAL_TOP_SHOWS,
    PODCAST_INTERNAL_SEARCH_RESULTS,
    PODCAST_INTERNAL_EPISODES,
    PODCAST_INTERNAL_SEEKING,
    PODCAST_INTERNAL_PLAYING,
    PODCAST_INTERNAL_DOWNLOAD_QUEUE
} PodcastInternalState;

// Module state
// Home and Episodes keep `scroll` in PIXELS, not rows - their renderers lay out
// section headers and an info panel between the items, so the window cannot be
// expressed as a row index. .external_scroll keeps ListNav off .scroll; the
// pixel offset lives in the plain int below, owned by the renderer.
static ListNav podcast_menu_nav = {
    .selected        = 0,
    .scroll          = 0,
    .count           = 0,
    .items_per_page  = 1,
    .external_scroll = true,
};
static int podcast_menu_scroll_px = 0;   // pixels
static ListNav podcast_manage_nav = {
    .selected       = 0,
    .scroll         = 0,
    .count          = PODCAST_MANAGE_COUNT,
    .items_per_page = 1,
};
static ListNav podcast_top_shows_nav = {
    .selected       = 0,
    .scroll         = 0,
    .count          = 0,
    .items_per_page = 1,
};
static ListNav podcast_search_nav = {
    .selected       = 0,
    .scroll         = 0,
    .count          = 0,
    .items_per_page = 1,
};
static char podcast_search_query[256] = "";
static ListNav podcast_episodes_nav = {
    .selected        = 0,
    .scroll          = 0,
    .count           = 0,
    .items_per_page  = 1,
    .external_scroll = true,
};
static int podcast_episodes_scroll_px = 0;   // pixels
static int podcast_current_feed_index = -1;
static int podcast_current_episode_index = -1;
// Guid of the item under the queue cursor, remembered across frames: the worker
// compacts COMPLETE and FAILED items out of wherever they sat, so a count delta
// alone cannot say how far the selection shifted.
static char podcast_queue_cursor_guid[PODCAST_MAX_GUID] = "";

static ListNav podcast_queue_nav = {
    .selected       = 0,
    .scroll         = 0,
    .count          = 0,
    .items_per_page = 1,
};
// The "Resuming..." message shown for as long as a seek takes. Screen-bound so
// that leaving the module mid-seek cannot carry it onto the next screen.
static ToastToken seek_toast = TOAST_TOKEN_NONE;

// Announce the position playback is being resumed at.
static void show_seek_toast(int feed_index, int episode_index) {
    PodcastEpisode* ep = Podcast_getEpisode(feed_index, episode_index);
    char msg[64];
    if (ep && ep->progress_sec > 0) {
        snprintf(msg, sizeof(msg), "Resuming at %d:%02d...",
                 ep->progress_sec / 60, ep->progress_sec % 60);
    } else {
        snprintf(msg, sizeof(msg), "Resuming...");
    }
    seek_toast = Toast_showScreenBound(msg, TOAST_DURATION_FOREVER);
}

// Periodic progress saving
static uint32_t last_progress_save_time = 0;
#define PROGRESS_SAVE_INTERVAL_MS 30000  // 30 seconds

// Confirmation dialog state
static bool show_confirm = false;
static int confirm_target_index = -1;
static char confirm_podcast_name[256] = "";
static int confirm_return_state = 0;  // 0 = menu, 1 = top_shows, 2 = search_results

// Screen off state
static bool screen_off = false;

// Handle USB/Bluetooth media button events
static void handle_hid_events(void) {
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
            if (Player_getState() == PLAYER_STATE_PAUSED) Player_play();
            else Player_pause();
        } else {
            ModuleCommon_handleHIDVolume(hid_event);
        }
    }
}

static void clear_and_show_screen_off_hint(SDL_Surface *screen) {
    GFX_clearLayers(LAYER_SCROLLTEXT);
    PLAT_clearLayers(LAYER_BUFFER);
    PLAT_clearLayers(LAYER_PODCAST_PROGRESS);
    PLAT_GPU_Flip();
    GFX_clear(screen);
    render_screen_off_hint(screen);
    GFX_flip(screen);
}

static void return_to_episodes(PodcastInternalState *state, int *dirty) {
    Podcast_flushProgress();
    Podcast_clearArtwork();
    Toast_dismiss(seek_toast);
    GFX_clearLayers(LAYER_SCROLLTEXT);
    PLAT_clearLayers(LAYER_BUFFER);
    PLAT_clearLayers(LAYER_PODCAST_PROGRESS);
    PLAT_GPU_Flip();
    ModuleCommon_setAutosleepDisabled(false);
    podcast_episodes_nav.selected = podcast_current_episode_index;
    *state = PODCAST_INTERNAL_EPISODES;
    *dirty = 1;
}

ModuleExitReason PodcastModule_run(SDL_Surface* screen) {
    Podcast_init();
    Keyboard_init();

    // Auto-check for new episodes once per app session
    static bool auto_refreshed = false;
    if (!auto_refreshed && Wifi_isConnected() && Podcast_getSubscriptionCount() > 0) {
        Podcast_startRefreshAll();
        auto_refreshed = true;
    }

    PodcastInternalState state = PODCAST_INTERNAL_MENU;
    int dirty = 1;
    int show_setting = 0;

    screen_off = false;
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();
    show_confirm = false;
    ListNav_scrollToTop(&podcast_menu_nav);
    podcast_menu_scroll_px = 0;

    // Re-enter playing state if podcast is playing in background
    if (Background_getActive() == BG_PODCAST && Podcast_isActive()) {
        Background_setActive(BG_NONE);
        ModuleCommon_setAutosleepDisabled(true);
        state = PODCAST_INTERNAL_PLAYING;
    }

    while (1) {
        ModuleCommon_frameBegin();

        // Handle confirmation dialog
        if (show_confirm) {
            if (PAD_justPressed(BTN_A)) {
                // Confirm unsubscribe
                Podcast_unsubscribe(confirm_target_index);
                Toast_show("Unsubscribed", TOAST_DURATION);
                show_confirm = false;
                Podcast_clearTitleScroll();
                dirty = 1;
                GFX_sync();
                continue;
            } else if (PAD_justPressed(BTN_B)) {
                show_confirm = false;
                Podcast_clearTitleScroll();
                dirty = 1;
                GFX_sync();
                continue;
            }
            // Render confirmation dialog (covers entire screen)
            render_confirmation_dialog(screen, confirm_podcast_name, "Unsubscribe?");
            GFX_flip(screen);
            GFX_sync();
            continue;
        }

        // Handle global input (skip if screen off or hint active)
        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            int app_state_for_help;
            switch (state) {
                case PODCAST_INTERNAL_MENU: app_state_for_help = 30; break;
                case PODCAST_INTERNAL_MANAGE: app_state_for_help = 31; break;
                case PODCAST_INTERNAL_TOP_SHOWS: app_state_for_help = 33; break;
                case PODCAST_INTERNAL_SEARCH_RESULTS: app_state_for_help = 34; break;
                case PODCAST_INTERNAL_EPISODES: app_state_for_help = 35; break;
                case PODCAST_INTERNAL_SEEKING: app_state_for_help = 37; break;
                case PODCAST_INTERNAL_PLAYING: app_state_for_help = 37; break;
                case PODCAST_INTERNAL_DOWNLOAD_QUEUE: app_state_for_help = 35; break;
                default: app_state_for_help = 30; break;
            }

            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help);
            if (global.should_quit) {
                Podcast_cleanup();
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                GFX_sync();
                continue;
            }
        }

        // =========================================
        // PODCAST MENU STATE (unified: continue listening + subscriptions)
        // =========================================
        if (state == PODCAST_INTERNAL_MENU) {
            Podcast_update();

            // Check for background refresh completion
            if (Podcast_checkRefreshCompleted()) {
                Podcast_saveSubscriptions();
                dirty = 1;
            }

            int cl_count_raw = Podcast_getContinueListeningCount();
            int cl_count = (cl_count_raw > PODCAST_CONTINUE_LISTENING_DISPLAY) ? PODCAST_CONTINUE_LISTENING_DISPLAY : cl_count_raw;
            int sub_count = Podcast_getSubscriptionCount();
            // "Downloads" item at the bottom (only visible when there are queued items)
            int dl_queue_count = 0;
            Podcast_getDownloadQueue(&dl_queue_count);
            int has_downloads_item = (dl_queue_count > 0) ? 1 : 0;
            int total = cl_count + sub_count + has_downloads_item;
            podcast_menu_nav.items_per_page = calc_list_layout(screen).rich_items_per_page;

            // Items change underneath the cursor (the Downloads row appears and
            // disappears with the queue), so reconcile before reading it.
            // The Downloads row comes and goes with the queue, so the row count
            // can change without the cursor moving.
            if (podcast_menu_nav.count != total) dirty = 1;
            if (ListNav_reconcile(&podcast_menu_nav, total).moved) dirty = 1;

            if (has_downloads_item) dirty = 1;  // Force redraw to update download status
            if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();
            if (Podcast_titleScrollNeedsRender()) dirty = 1;
            if (Podcast_loadPendingThumbnails()) dirty = 1;

            if (ListNav_step(&podcast_menu_nav, ListNavPad_read()).moved) {
                Podcast_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && total > 0) {
                if (has_downloads_item && podcast_menu_nav.selected == cl_count + sub_count) {
                    // Downloads item — open download queue
                    ListNav_scrollToTop(&podcast_queue_nav);
                    Podcast_clearTitleScroll();
                    state = PODCAST_INTERNAL_DOWNLOAD_QUEUE;
                    dirty = 1;
                } else if (podcast_menu_nav.selected < cl_count) {
                    // Continue Listening item — play directly
                    ContinueListeningEntry* cl_entry = Podcast_getContinueListening(podcast_menu_nav.selected);
                    if (cl_entry) {
                        int fi = Podcast_findFeedIndex(cl_entry->feed_url);
                        if (fi >= 0) {
                            PodcastFeed* feed = Podcast_getSubscription(fi);
                            // Find episode index by guid
                            int ep_idx = -1;
                            if (feed) {
                                for (int e = 0; e < feed->episode_count; e++) {
                                    PodcastEpisode* ep = Podcast_getEpisode(fi, e);
                                    if (ep && strcmp(ep->guid, cl_entry->episode_guid) == 0) {
                                        ep_idx = e;
                                        break;
                                    }
                                }
                            }
                            if (feed && ep_idx >= 0 && Podcast_episodeFileExists(feed, ep_idx)) {
                                Background_stopAll();
                                podcast_current_feed_index = fi;
                                podcast_current_episode_index = ep_idx;
                                int load_result = Podcast_loadAndSeek(feed, ep_idx);
                                if (load_result >= 0) {
                                    Podcast_clearTitleScroll();
                                    ModuleCommon_recordInputTime();
                                    last_progress_save_time = SDL_GetTicks();
                                    if (load_result == 1) {
                                        show_seek_toast(fi, ep_idx);
                                        state = PODCAST_INTERNAL_SEEKING;
                                    } else {
                                        Player_play();
                                        state = PODCAST_INTERNAL_PLAYING;
                                    }
                                    // Update continue listening (move to top)
                                    Podcast_updateContinueListening(feed->feed_url, feed->feed_id,
                                        cl_entry->episode_guid, cl_entry->episode_title,
                                        feed->title, feed->artwork_url);
                                } else {
                                    Toast_show("Failed to play", TOAST_DURATION);
                                }
                            } else {
                                Toast_show("Episode not available", TOAST_DURATION);
                            }
                        } else {
                            Toast_show("Podcast not found", TOAST_DURATION);
                        }
                    }
                } else {
                    // Subscription item — go to episodes
                    podcast_current_feed_index = podcast_menu_nav.selected - cl_count;
                    ListNav_scrollToTop(&podcast_episodes_nav);
                    podcast_episodes_scroll_px = 0;
                    Podcast_clearTitleScroll();
                    state = PODCAST_INTERNAL_EPISODES;
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && total > 0) {
                // Only allow unsubscribe on subscription items (not Downloads item)
                if (podcast_menu_nav.selected >= cl_count && podcast_menu_nav.selected < cl_count + sub_count) {
                    int sub_idx = podcast_menu_nav.selected - cl_count;
                    PodcastFeed* feed = Podcast_getSubscription(sub_idx);
                    if (feed) {
                        strncpy(confirm_podcast_name, feed->title, sizeof(confirm_podcast_name) - 1);
                        confirm_podcast_name[sizeof(confirm_podcast_name) - 1] = '\0';
                        confirm_target_index = sub_idx;
                        confirm_return_state = 0;
                        Podcast_clearTitleScroll();
                        show_confirm = true;
                        dirty = 1;
                    }
                }
            }
            else if (PAD_justPressed(BTN_Y)) {
                ListNav_scrollToTop(&podcast_manage_nav);
                Podcast_clearTitleScroll();
                state = PODCAST_INTERNAL_MANAGE;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                Podcast_clearTitleScroll();
                if (Podcast_isActive() || Podcast_isDownloading()) {
                    Podcast_saveSubscriptions();
                    Podcast_flushProgress();
                    if (Podcast_isActive()) {
                        Background_setActive(BG_PODCAST);
                    }
                } else {
                    Podcast_cleanup();
                }
                return MODULE_EXIT_TO_MENU;
            }
        }
        // =========================================
        // MANAGE STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_MANAGE) {
            Podcast_update();
            podcast_manage_nav.items_per_page = calc_list_layout(screen).items_per_page;

            if (ListNav_step(&podcast_manage_nav, ListNavPad_read()).moved) {
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                switch (podcast_manage_nav.selected) {
                    case PODCAST_MANAGE_SEARCH: {
                        if (!Wifi_ensureConnected(screen, show_setting)) {
                            Toast_show("Internet connection required", TOAST_DURATION);
                            dirty = 1;
                            break;
                        }
                        DisplayHelper_prepareForExternal();
                        char* query = Keyboard_open("Search podcasts");
                        PAD_poll(); PAD_reset();
                        DisplayHelper_recoverDisplay();
					{
						SDL_Surface* ns = DisplayHelper_getReinitScreen();
						if (ns) screen = ns;
					}
                        SDL_Delay(100);
                        PAD_poll(); PAD_reset();
                        if (query && query[0]) {
                            strncpy(podcast_search_query, query, sizeof(podcast_search_query) - 1);
                            Podcast_startSearch(podcast_search_query);
                            ListNav_scrollToTop(&podcast_search_nav);
                            state = PODCAST_INTERNAL_SEARCH_RESULTS;
                        }
                        if (query) free(query);
                        dirty = 1;
                        break;
                    }
                    case PODCAST_MANAGE_TOP_SHOWS:
                        if (!Wifi_ensureConnected(screen, show_setting)) {
                            Toast_show("Internet connection required", TOAST_DURATION);
                            dirty = 1;
                            break;
                        }
                        Podcast_loadCharts(NULL);
                        ListNav_scrollToTop(&podcast_top_shows_nav);
                        state = PODCAST_INTERNAL_TOP_SHOWS;
                        dirty = 1;
                        break;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                state = PODCAST_INTERNAL_MENU;
                dirty = 1;
            }
        }
        // =========================================
        // TOP SHOWS STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_TOP_SHOWS) {
            Podcast_update();
            const PodcastChartsStatus* chart_status = Podcast_getChartsStatus();
            podcast_top_shows_nav.items_per_page = calc_list_layout(screen).rich_items_per_page;

            if (chart_status->loading || chart_status->completed) dirty = 1;
            if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();
            if (Podcast_titleScrollNeedsRender()) dirty = 1;

            if (!chart_status->loading) {
                int count = 0;
                Podcast_getTopShows(&count);
                if (ListNav_reconcile(&podcast_top_shows_nav, count).moved) dirty = 1;

                if (ListNav_step(&podcast_top_shows_nav, ListNavPad_read()).moved) {
                    Podcast_clearTitleScroll();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_A) && count > 0) {
                    PodcastChartItem* items = Podcast_getTopShows(&count);
                    if (podcast_top_shows_nav.selected < count) {
                        bool already_subscribed = Podcast_isSubscribedByItunesId(items[podcast_top_shows_nav.selected].itunes_id);
                        if (already_subscribed) {
                            // Find subscription index and show confirm dialog
                            int sub_count = 0;
                            PodcastFeed* feeds = Podcast_getSubscriptions(&sub_count);
                            for (int si = 0; si < sub_count; si++) {
                                if (feeds[si].itunes_id[0] && strcmp(feeds[si].itunes_id, items[podcast_top_shows_nav.selected].itunes_id) == 0) {
                                    strncpy(confirm_podcast_name, items[podcast_top_shows_nav.selected].title, sizeof(confirm_podcast_name) - 1);
                                    confirm_podcast_name[sizeof(confirm_podcast_name) - 1] = '\0';
                                    confirm_target_index = si;
                                    confirm_return_state = 1;
                                    show_confirm = true;
                                    break;
                                }
                            }
                        } else {
                            Podcast_clearTitleScroll();
                            render_podcast_loading(screen, "Subscribing...");
                            GFX_flip(screen);
                            int sub_result = Podcast_subscribeFromItunes(items[podcast_top_shows_nav.selected].itunes_id);
                            if (sub_result == 0) {
                                Toast_show("Subscribed!", TOAST_DURATION);
                            } else {
                                const char* err = Podcast_getError();
                                Toast_show(err && err[0] ? err : "Subscribe failed", TOAST_DURATION);
                            }
                        }
                    }
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_X)) {
                    // Refresh charts - clear cache and reload
                    if (!Wifi_ensureConnected(screen, show_setting)) {
                        Toast_show("Internet connection required", TOAST_DURATION);
                    } else {
                        Podcast_clearChartsCache();
                        Podcast_loadCharts(NULL);
                        ListNav_scrollToTop(&podcast_top_shows_nav);
                        Toast_show("Refreshing...", TOAST_DURATION);
                    }
                    dirty = 1;
                }
            }

            if (PAD_justPressed(BTN_B)) {
                Podcast_clearTitleScroll();
                state = PODCAST_INTERNAL_MANAGE;
                dirty = 1;
            }
        }
        // =========================================
        // SEARCH RESULTS STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_SEARCH_RESULTS) {
            Podcast_update();
            const PodcastSearchStatus* search_status = Podcast_getSearchStatus();
            podcast_search_nav.items_per_page = calc_list_layout(screen).rich_items_per_page;

            if (search_status->searching || search_status->completed) dirty = 1;
            if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();
            if (Podcast_titleScrollNeedsRender()) dirty = 1;

            if (!search_status->searching) {
                int count = 0;
                Podcast_getSearchResults(&count);
                if (ListNav_reconcile(&podcast_search_nav, count).moved) dirty = 1;

                if (ListNav_step(&podcast_search_nav, ListNavPad_read()).moved) {
                    Podcast_clearTitleScroll();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_A) && count > 0) {
                    PodcastSearchResult* results = Podcast_getSearchResults(&count);
                    if (podcast_search_nav.selected < count) {
                        bool already_subscribed = results[podcast_search_nav.selected].feed_url[0] &&
                                                   Podcast_isSubscribed(results[podcast_search_nav.selected].feed_url);
                        if (already_subscribed) {
                            // Find subscription index and show confirm dialog
                            int sub_count = 0;
                            PodcastFeed* feeds = Podcast_getSubscriptions(&sub_count);
                            for (int si = 0; si < sub_count; si++) {
                                if (strcmp(feeds[si].feed_url, results[podcast_search_nav.selected].feed_url) == 0) {
                                    strncpy(confirm_podcast_name, results[podcast_search_nav.selected].title, sizeof(confirm_podcast_name) - 1);
                                    confirm_podcast_name[sizeof(confirm_podcast_name) - 1] = '\0';
                                    confirm_target_index = si;
                                    confirm_return_state = 2;
                                    show_confirm = true;
                                    break;
                                }
                            }
                        } else {
                            Podcast_clearTitleScroll();
                            render_podcast_loading(screen, "Subscribing...");
                            GFX_flip(screen);
                            int sub_result;
                            if (results[podcast_search_nav.selected].feed_url[0]) {
                                sub_result = Podcast_subscribe(results[podcast_search_nav.selected].feed_url);
                            } else {
                                sub_result = Podcast_subscribeFromItunes(results[podcast_search_nav.selected].itunes_id);
                            }
                            if (sub_result == 0) {
                                Toast_show("Subscribed!", TOAST_DURATION);
                            } else {
                                const char* err = Podcast_getError();
                                Toast_show(err && err[0] ? err : "Subscribe failed", TOAST_DURATION);
                            }
                        }
                    }
                    dirty = 1;
                }
            }

            if (PAD_justPressed(BTN_B)) {
                Podcast_clearTitleScroll();
                Podcast_cancelSearch();
                state = PODCAST_INTERNAL_MANAGE;
                dirty = 1;
            }
        }
        // =========================================
        // EPISODES STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_EPISODES) {
            PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
            int count = feed ? feed->episode_count : 0;
            podcast_episodes_nav.items_per_page = calc_list_layout(screen).rich_items_per_page;

            // Check if refresh just completed
            if (Podcast_checkRefreshCompleted()) {
                feed = Podcast_getSubscription(podcast_current_feed_index);
                count = feed ? feed->episode_count : 0;
                Podcast_invalidateEpisodeCache();
                if (feed && feed->new_episode_count > 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "%d new episode%s found!", feed->new_episode_count,
                             feed->new_episode_count > 1 ? "s" : "");
                    Toast_show(msg, TOAST_DURATION);
                } else {
                    Toast_show("Already up to date", TOAST_DURATION);
                }
                Podcast_saveSubscriptions();
                dirty = 1;
            }

            // Force redraw when downloads active
            int queue_count = 0;
            PodcastDownloadItem* queue = Podcast_getDownloadQueue(&queue_count);
            for (int i = 0; i < queue_count; i++) {
                if (queue[i].status == PODCAST_DOWNLOAD_DOWNLOADING || queue[i].status == PODCAST_DOWNLOAD_PENDING) {
                    dirty = 1;
                    break;
                }
            }

            if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();
            if (Podcast_titleScrollNeedsRender()) dirty = 1;

            if (ListNav_reconcile(&podcast_episodes_nav, count).moved) dirty = 1;
            if (ListNav_step(&podcast_episodes_nav, ListNavPad_read()).moved) {
                Podcast_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && count > 0 && feed) {
                podcast_current_episode_index = podcast_episodes_nav.selected;
                PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);

                if (ep) {
                    int dl_progress = 0;
                    int dl_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, ep->guid, &dl_progress);

                    if (dl_status == PODCAST_DOWNLOAD_DOWNLOADING || dl_status == PODCAST_DOWNLOAD_PENDING) {
                        // Cancel download
                        Toast_show(Podcast_cancelEpisodeDownload(feed->feed_url, ep->guid) == 0
                                       ? "Download cancelled" : "Cancel failed", TOAST_DURATION);
                    } else if (Podcast_episodeFileExists(feed, podcast_current_episode_index)) {
                        Background_stopAll();
                        int load_result = Podcast_loadAndSeek(feed, podcast_current_episode_index);
                        if (load_result >= 0) {
                            // Clear new flag only when actually playing
                            Podcast_clearNewFlag(podcast_current_feed_index, podcast_current_episode_index);
                            Podcast_clearTitleScroll();
                            ModuleCommon_recordInputTime();
                            last_progress_save_time = SDL_GetTicks();
                            // Update continue listening
                            Podcast_updateContinueListening(feed->feed_url, feed->feed_id,
                                ep->guid, ep->title, feed->title, feed->artwork_url);
                            if (load_result == 1) {
                                // Has saved progress — seeking, show player UI while waiting
                                show_seek_toast(podcast_current_feed_index, podcast_current_episode_index);
                                state = PODCAST_INTERNAL_SEEKING;
                            } else {
                                // No saved progress — play immediately
                                Player_play();
                                state = PODCAST_INTERNAL_PLAYING;
                            }
                        } else {
                            Toast_show("Failed to play", TOAST_DURATION);
                        }
                    } else {
                        if (!Wifi_ensureConnected(screen, show_setting)) {
                            Toast_show("No network connection", TOAST_DURATION);
                        } else if (Podcast_queueDownload(feed, podcast_current_episode_index) == 0) {
                            Toast_show("Downloading...", TOAST_DURATION);
                        } else {
                            Toast_show("Download failed", TOAST_DURATION);
                        }
                    }
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && count > 0 && feed) {
                PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_episodes_nav.selected);
                if (ep) {
                    // Toggle played status
                    if (ep->progress_sec == -1) {
                        ep->progress_sec = 0;
                        Podcast_saveProgress(feed->feed_url, ep->guid, 0);
                        Toast_show("Marked as unplayed", TOAST_DURATION);
                    } else {
                        ep->progress_sec = -1;
                        Podcast_markAsPlayed(feed->feed_url, ep->guid);
                        Podcast_removeContinueListening(feed->feed_url, ep->guid);
                        Toast_show("Marked as played", TOAST_DURATION);
                    }
                    Podcast_flushProgress();
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_Y) && feed) {
                if (Podcast_isRefreshing()) {
                    Toast_show("Already refreshing...", TOAST_DURATION);
                } else if (!Wifi_ensureConnected(screen, show_setting)) {
                    Toast_show("No network connection", TOAST_DURATION);
                } else {
                    Podcast_startRefreshFeed(podcast_current_feed_index);
                    Toast_show("Checking for new episodes...", TOAST_DURATION);
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                Podcast_clearTitleScroll();
                state = PODCAST_INTERNAL_MENU;
                dirty = 1;
            }
        }
        // =========================================
        // DOWNLOAD QUEUE STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_DOWNLOAD_QUEUE) {
            int queue_count = 0;
            PodcastDownloadItem* qitems = Podcast_getDownloadQueue(&queue_count);
            podcast_queue_nav.items_per_page = calc_list_layout(screen).rich_items_per_page;

            if (podcast_queue_nav.count != queue_count) {
                // Find where the selected item went. What vanished from above it
                // is what shifted the selection; moving the window by the same
                // amount holds it on its screen row. Redraw regardless - rows
                // below the cursor repaint even when the cursor does not move.
                int now_at = -1;
                if (podcast_queue_cursor_guid[0]) {
                    for (int i = 0; i < queue_count; i++) {
                        if (strcmp(qitems[i].episode_guid, podcast_queue_cursor_guid) == 0) {
                            now_at = i;
                            break;
                        }
                    }
                }
                if (now_at >= 0 && now_at < podcast_queue_nav.selected) {
                    ListNav_onItemsRemoved(&podcast_queue_nav, 0,
                                           podcast_queue_nav.selected - now_at);
                }
                ListNav_reconcile(&podcast_queue_nav, queue_count);
                Podcast_clearTitleScroll();
                dirty = 1;
            }

            // Force redraw when downloads are active
            for (int qi = 0; qi < queue_count; qi++) {
                PodcastDownloadItem* qitem = &Podcast_getDownloadQueue(NULL)[qi];
                if (qitem->status == PODCAST_DOWNLOAD_DOWNLOADING || qitem->status == PODCAST_DOWNLOAD_PENDING) {
                    dirty = 1;
                    break;
                }
            }

            if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();
            if (Podcast_titleScrollNeedsRender()) dirty = 1;

            // Remember what the cursor is on, for the next count change.
            if (podcast_queue_nav.selected >= 0 && podcast_queue_nav.selected < queue_count) {
                snprintf(podcast_queue_cursor_guid, sizeof(podcast_queue_cursor_guid), "%s",
                         qitems[podcast_queue_nav.selected].episode_guid);
            } else {
                podcast_queue_cursor_guid[0] = '\0';
            }

            if (ListNav_step(&podcast_queue_nav, ListNavPad_read()).moved) {
                Podcast_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && queue_count > 0) {
                // Cancel/remove selected item
                PodcastDownloadItem* queue = Podcast_getDownloadQueue(NULL);
                if (podcast_queue_nav.selected < queue_count) {
                    PodcastDownloadItem* sel = &queue[podcast_queue_nav.selected];
                    if (Podcast_cancelEpisodeDownload(sel->feed_url, sel->episode_guid) == 0) {
                        Toast_show("Download removed", TOAST_DURATION);
                        ListNav_onItemRemoved(&podcast_queue_nav, podcast_queue_nav.selected);
                        podcast_queue_cursor_guid[0] = '\0';
                    } else {
                        Toast_show("Remove failed", TOAST_DURATION);
                    }
                    Podcast_clearTitleScroll();
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                Podcast_clearTitleScroll();
                state = PODCAST_INTERNAL_MENU;
                dirty = 1;
            }
        }
        // =========================================
        // SEEKING STATE (resuming to saved position)
        // =========================================
        else if (state == PODCAST_INTERNAL_SEEKING) {
            ModuleCommon_setAutosleepDisabled(true);

            if (!Player_resume()) {
                // Seek complete — start playback
                Player_play();
                Toast_dismiss(seek_toast);
                ModuleCommon_recordInputTime();
                last_progress_save_time = SDL_GetTicks();
                state = PODCAST_INTERNAL_PLAYING;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                // Cancel seeking — stop and go back
                Podcast_stop();
                return_to_episodes(&state, &dirty);
                continue;
            }

            dirty = 1;  // Keep refreshing to show seeking status
        }
        // =========================================
        // PLAYING STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_PLAYING) {
            ModuleCommon_setAutosleepDisabled(true);

            // Handle screen off hint
            if (ModuleCommon_isScreenOffHintActive()) {
                handle_hid_events();
                ModuleCommon_handleHardwareVolume();
                Podcast_update();

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
            else if (screen_off) {
                handle_hid_events();
                ModuleCommon_handleHardwareVolume();
                Podcast_update();

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
            else {
                if (PAD_justPressed(BTN_A)) {
                    if (Player_getState() == PLAYER_STATE_PAUSED) Player_play();
                    else Player_pause();
                    ModuleCommon_recordInputTime();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_B)) {
                    if (Player_getState() == PLAYER_STATE_PLAYING) {
                        // Playing — let audio continue in background
                        Podcast_flushProgress();
                        Podcast_clearArtwork();
                        GFX_clearLayers(LAYER_SCROLLTEXT);
                        PLAT_clearLayers(LAYER_BUFFER);
                        PLAT_clearLayers(LAYER_PODCAST_PROGRESS);
                        PLAT_GPU_Flip();
                        podcast_episodes_nav.selected = podcast_current_episode_index;
                        state = PODCAST_INTERNAL_EPISODES;
                        dirty = 1;
                    } else {
                        // Paused — stop and go back normally
                        Podcast_stop();
                        return_to_episodes(&state, &dirty);
                        continue;
                    }
                }
                else if (PAD_tappedSelect(SDL_GetTicks())) {
                    ModuleCommon_startScreenOffHint();
                    clear_and_show_screen_off_hint(screen);
                    continue;
                }
                else if (PAD_justRepeated(BTN_LEFT)) {
                    int pos_ms = Player_getPosition();
                    Player_seek(pos_ms - 10000 < 0 ? 0 : pos_ms - 10000);
                    ModuleCommon_recordInputTime();
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_RIGHT)) {
                    int pos_ms = Player_getPosition();
                    int dur_ms = Player_getDuration();
                    Player_seek(pos_ms + 30000 > dur_ms ? dur_ms : pos_ms + 30000);
                    ModuleCommon_recordInputTime();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_UP)) {
                    float speed = Player_getPlaybackSpeed();
                    speed += 0.25f;
                    if (speed > 2.0f) speed = 2.0f;
                    Player_setPlaybackSpeed(speed);
                    char msg[32];
                    snprintf(msg, sizeof(msg), "Speed: %.2gx", speed);
                    Toast_show(msg, TOAST_DURATION);
                    ModuleCommon_recordInputTime();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_DOWN)) {
                    float speed = Player_getPlaybackSpeed();
                    speed -= 0.25f;
                    if (speed < 0.5f) speed = 0.5f;
                    Player_setPlaybackSpeed(speed);
                    char msg[32];
                    snprintf(msg, sizeof(msg), "Speed: %.2gx", speed);
                    Toast_show(msg, TOAST_DURATION);
                    ModuleCommon_recordInputTime();
                    dirty = 1;
                }

                Podcast_update();
                if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();
                if (Podcast_titleScrollNeedsRender()) dirty = 1;

                // Periodic progress saving (every 30 seconds)
                {
                    uint32_t now = SDL_GetTicks();
                    if (Podcast_isActive() && now - last_progress_save_time >= PROGRESS_SAVE_INTERVAL_MS) {
                        PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
                        if (feed) {
                            PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);
                            if (ep) {
                                int position = Player_getPosition();
                                if (position > 0) {
                                    ep->progress_sec = position / 1000;
                                    Podcast_saveProgress(feed->feed_url, ep->guid, ep->progress_sec);
                                    Podcast_flushProgress();
                                }
                            }
                        }
                        last_progress_save_time = now;
                    }
                }

                // Detect episode end (player stopped naturally)
                if (Player_getState() == PLAYER_STATE_STOPPED) {
                    PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
                    PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);
                    char saved_feed_url[PODCAST_MAX_URL] = "";
                    char saved_guid[PODCAST_MAX_GUID] = "";
                    if (feed && ep) {
                        strncpy(saved_feed_url, feed->feed_url, PODCAST_MAX_URL - 1);
                        strncpy(saved_guid, ep->guid, PODCAST_MAX_GUID - 1);
                    }

                    Podcast_stop();

                    if (saved_feed_url[0] && saved_guid[0]) {
                        Podcast_markAsPlayed(saved_feed_url, saved_guid);
                        Podcast_removeContinueListening(saved_feed_url, saved_guid);
                    }
                    if (ep) ep->progress_sec = -1;

                    return_to_episodes(&state, &dirty);
                    continue;
                }

                // GPU progress bar update (updates every second without full redraw)
                if (PodcastProgress_needsRefresh()) {
                    PodcastProgress_renderGPU();
                }

                // Auto screen-off
                if (Podcast_isActive() && ModuleCommon_checkAutoScreenOffTimeout()) {
                    clear_and_show_screen_off_hint(screen);
                    continue;
                }
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
                    case PODCAST_INTERNAL_MENU:
                        render_podcast_main_page(screen, show_setting, podcast_menu_nav.selected, &podcast_menu_scroll_px);
                        break;
                    case PODCAST_INTERNAL_MANAGE:
                        render_podcast_manage(screen, show_setting, podcast_manage_nav.selected,
                                              podcast_manage_nav.scroll, Podcast_getSubscriptionCount());
                        break;
                    case PODCAST_INTERNAL_TOP_SHOWS:
                        render_podcast_top_shows(screen, show_setting, podcast_top_shows_nav.selected, &podcast_top_shows_nav.scroll);
                        break;
                    case PODCAST_INTERNAL_SEARCH_RESULTS:
                        render_podcast_search_results(screen, show_setting, podcast_search_nav.selected, &podcast_search_nav.scroll);
                        break;
                    case PODCAST_INTERNAL_EPISODES:
                        render_podcast_episodes(screen, show_setting, podcast_current_feed_index, podcast_episodes_nav.selected,
                                                &podcast_episodes_scroll_px);
                        break;
                    case PODCAST_INTERNAL_SEEKING:
                        render_podcast_playing(screen, show_setting, podcast_current_feed_index, podcast_current_episode_index);
                        break;
                    case PODCAST_INTERNAL_PLAYING:
                        render_podcast_playing(screen, show_setting, podcast_current_feed_index, podcast_current_episode_index);
                        break;
                    case PODCAST_INTERNAL_DOWNLOAD_QUEUE:
                        render_podcast_download_queue(screen, show_setting, podcast_queue_nav.selected, &podcast_queue_nav.scroll);
                        break;
                }
            }

            if (show_setting && state != PODCAST_INTERNAL_PLAYING) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;
        } else if (!screen_off) {
            GFX_sync();
        }
    }
}

// Check if podcast module is active (playing)
bool PodcastModule_isActive(void) {
    return Podcast_isActive();
}

// Background tick: detect episode end and save progress while in menu
void PodcastModule_backgroundTick(void) {
    Podcast_update();

    // Periodic progress saving
    uint32_t now = SDL_GetTicks();
    if (Podcast_isActive() && now - last_progress_save_time >= PROGRESS_SAVE_INTERVAL_MS) {
        PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
        if (feed) {
            PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);
            if (ep) {
                int position = Player_getPosition();
                if (position > 0) {
                    ep->progress_sec = position / 1000;
                    Podcast_saveProgress(feed->feed_url, ep->guid, ep->progress_sec);
                    Podcast_flushProgress();
                }
            }
        }
        last_progress_save_time = now;
    }

    // Detect episode end
    if (Player_getState() == PLAYER_STATE_STOPPED) {
        PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
        PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);
        char saved_feed_url[PODCAST_MAX_URL] = "";
        char saved_guid[PODCAST_MAX_GUID] = "";
        if (feed && ep) {
            strncpy(saved_feed_url, feed->feed_url, PODCAST_MAX_URL - 1);
            strncpy(saved_guid, ep->guid, PODCAST_MAX_GUID - 1);
        }

        Podcast_stop();

        if (saved_feed_url[0] && saved_guid[0]) {
            Podcast_markAsPlayed(saved_feed_url, saved_guid);
            Podcast_removeContinueListening(saved_feed_url, saved_guid);
        }
        if (ep) ep->progress_sec = -1;

        Background_setActive(BG_NONE);
        ModuleCommon_setAutosleepDisabled(false);
    }
}
