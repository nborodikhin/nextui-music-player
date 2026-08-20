#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_downloader.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "ui_icons.h"
#include "module_common.h"

// Scroll text state for YouTube results (selected item)
static ScrollTextState downloader_results_scroll_text = {0};

// Scroll text state for YouTube download queue (selected item)
static ScrollTextState downloader_queue_scroll_text = {0};

// YouTube sub-menu items
static const char* youtube_menu_items[] = {"Search Music", "Download Queue"};
#define YOUTUBE_MENU_COUNT 2

// Format download speed for display
static void format_download_speed(char* buf, int buf_size, int bytes_per_sec) {
    if (bytes_per_sec <= 0) {
        snprintf(buf, buf_size, "0 B/s");
    } else if (bytes_per_sec < 1024) {
        snprintf(buf, buf_size, "%d B/s", bytes_per_sec);
    } else if (bytes_per_sec < 1024 * 1024) {
        snprintf(buf, buf_size, "%.1f KB/s", bytes_per_sec / 1024.0);
    } else {
        snprintf(buf, buf_size, "%.1f MB/s", bytes_per_sec / (1024.0 * 1024.0));
    }
}

// Format ETA for display
static void format_download_eta(char* buf, int buf_size, int seconds) {
    if (seconds <= 0) {
        buf[0] = '\0';
    } else if (seconds < 60) {
        snprintf(buf, buf_size, "%ds", seconds);
    } else if (seconds < 3600) {
        snprintf(buf, buf_size, "%dm%ds", seconds / 60, seconds % 60);
    } else {
        snprintf(buf, buf_size, "%dh%dm", seconds / 3600, (seconds % 3600) / 60);
    }
}

// Label callback for queue count on Download Queue menu item
static const char* youtube_menu_get_label(int index, const char* default_label,
                                          char* buffer, int buffer_size) {
    if (index == 1) {  // Download Queue
        int qcount = Downloader_queueCount();
        if (qcount > 0) {
            snprintf(buffer, buffer_size, "Download Queue (%d)", qcount);
            return buffer;
        }
    }
    return NULL;  // Use default label
}

// Render YouTube sub-menu
void render_downloader_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                         int menu_scroll) {
    SimpleMenuConfig config = {
        .title = "Downloader",
        .items = youtube_menu_items,
        .item_count = YOUTUBE_MENU_COUNT,
        .btn_b_label = "BACK",
        .get_label = youtube_menu_get_label,
        .render_badge = NULL,
        .get_icon = NULL
    };
    render_simple_menu(screen, show_setting, menu_selected, menu_scroll, &config);
}

// Render YouTube searching status
void render_downloader_searching(SDL_Surface* screen, int show_setting, const char* search_query) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "Searching...", show_setting);

    // Searching message
    char search_msg[300];
    snprintf(search_msg, sizeof(search_msg), "Searching for: %s", search_query);
    SDL_Surface* query_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), search_msg, COLOR_GRAY);
    if (query_text) {
        int qx = (hw - query_text->w) / 2;
        if (qx < SCALE1(PADDING)) qx = SCALE1(PADDING);
        SDL_BlitSurface(query_text, NULL, screen, &(SDL_Rect){qx, hh / 2 - SCALE1(30)});
        SDL_FreeSurface(query_text);
    }

    // Loading indicator
    const char* loading = "Please wait...";
    SDL_Surface* load_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), loading, COLOR_WHITE);
    if (load_text) {
        SDL_BlitSurface(load_text, NULL, screen, &(SDL_Rect){(hw - load_text->w) / 2, hh / 2 + SCALE1(10)});
        SDL_FreeSurface(load_text);
    }
}

// Render YouTube search results
void render_downloader_results(SDL_Surface* screen, int show_setting,
                            const char* search_query,
                            DownloaderResult* results, int result_count,
                            int selected, int* scroll, bool searching) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    // Title with search query
    char title[128];
    snprintf(title, sizeof(title), "Results: %s", search_query);
    render_screen_header(screen, title, show_setting);

    // Use common list layout calculation
    ListLayout layout = calc_list_layout(screen);

    // Adjust scroll (only if there's a selection)
    if (selected >= 0) {
        adjust_list_scroll(selected, scroll, layout.items_per_page);
    }

    // Reserve space for duration on the right (format: "99:59" max)
    int dur_w, dur_h;
    TTF_SizeUTF8(Fonts_getTiny(), "99:59", &dur_w, &dur_h);
    int duration_reserved = dur_w + SCALE1(PADDING * 2);  // Duration width + gap
    int max_width = layout.max_width - duration_reserved;

    for (int i = 0; i < layout.items_per_page && *scroll + i < result_count; i++) {
        int idx = *scroll + i;
        DownloaderResult* result = &results[idx];
        bool is_selected = (idx == selected);
        bool in_queue = Downloader_isInQueue(result->video_id);

        int y = layout.list_y + i * layout.item_h;

        // Calculate indicator width if in queue
        int indicator_width = 0;
        if (in_queue) {
            int ind_w, ind_h;
            TTF_SizeUTF8(Fonts_getTiny(), "[+]", &ind_w, &ind_h);
            indicator_width = ind_w + SCALE1(4);
        }

        // Calculate text width for pill sizing
        int pill_width = Fonts_calcListPillWidth(Fonts_getMedium(), result->title, truncated, max_width, indicator_width);

        // Background pill (sized to text width)
        SDL_Rect pill_rect = {SCALE1(PADDING), y, pill_width, layout.item_h};
        Fonts_drawListItemBg(screen, &pill_rect, is_selected);

        int title_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
        int text_y = y + (layout.item_h - TTF_FontHeight(Fonts_getMedium())) / 2;

        // Show indicator if already in queue
        if (in_queue) {
            SDL_Surface* indicator = TTF_RenderUTF8_Blended(Fonts_getTiny(), "[+]", is_selected ? uintToColour(THEME_COLOR5_255) : COLOR_GRAY);
            if (indicator) {
                SDL_BlitSurface(indicator, NULL, screen, &(SDL_Rect){title_x, y + (layout.item_h - indicator->h) / 2});
                title_x += indicator->w + SCALE1(4);
                SDL_FreeSurface(indicator);
            }
        }

        // Title - use common text rendering with scrolling for selected items
        int title_max_w = pill_width - SCALE1(BUTTON_PADDING * 2) - indicator_width;
        render_list_item_text(screen, &downloader_results_scroll_text, result->title, Fonts_getMedium(),
                              title_x, text_y, title_max_w, is_selected);

        // Duration (always on right, outside pill)
        if (result->duration_sec > 0) {
            char dur[16];
            int m = result->duration_sec / 60;
            int s = result->duration_sec % 60;
            snprintf(dur, sizeof(dur), "%d:%02d", m, s);
            SDL_Surface* dur_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), dur, COLOR_GRAY);
            if (dur_text) {
                SDL_BlitSurface(dur_text, NULL, screen, &(SDL_Rect){hw - dur_text->w - SCALE1(PADDING * 2), y + (layout.item_h - dur_text->h) / 2});
                SDL_FreeSurface(dur_text);
            }
        }
    }

    // Empty results message
    if (result_count == 0) {
        const char* msg = searching ? "Searching..." : "No results found";
        SDL_Surface* text = TTF_RenderUTF8_Blended(Fonts_getLarge(), msg, COLOR_GRAY);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, hh / 2 - text->h / 2});
            SDL_FreeSurface(text);
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);

    // Dynamic hint based on queue status (only show A action if item is selected)
    if (selected >= 0 && result_count > 0) {
        const char* action_hint = "DOWNLOAD";
        DownloaderResult* selected_result = &results[selected];
        if (Downloader_isInQueue(selected_result->video_id)) {
            action_hint = "QUEUED";
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", (char*)action_hint, NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }
}

// Render YouTube download queue (podcast-style with progress/speed/ETA)
void render_downloader_queue(SDL_Surface* screen, int show_setting,
                          int queue_selected, int* queue_scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    int qcount = 0;
    DownloaderQueueItem* queue = Downloader_queueGet(&qcount);
    const DownloaderDownloadStatus* dl_status = Downloader_getDownloadStatus();

    // Title with completion count
    char title[64];
    if (dl_status->total_items > 0) {
        snprintf(title, sizeof(title), "Downloads (%d/%d)",
                 dl_status->completed_count, dl_status->total_items);
    } else {
        snprintf(title, sizeof(title), "Download Queue");
    }
    render_screen_header(screen, title, show_setting);

    // Empty queue message
    if (qcount == 0) {
        downloader_queue_clear_scroll();
        render_empty_state(screen, "Queue is empty", "Search and add songs to download", NULL);
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // Two-row pill layout (like podcast download queue)
    ListLayout layout = calc_list_layout(screen);
    layout.item_h = layout.rich_item_h;
    layout.items_per_page = layout.rich_items_per_page;
    adjust_list_scroll(queue_selected, queue_scroll, layout.items_per_page);

    for (int i = 0; i < layout.items_per_page && *queue_scroll + i < qcount; i++) {
        int idx = *queue_scroll + i;
        DownloaderQueueItem* item = &queue[idx];
        bool is_selected = (idx == queue_selected);

        int y = layout.list_y + i * layout.item_h;

        // Build subtitle string for pill sizing (must reflect actual rendered width)
        char subtitle[128] = "";
        switch (item->status) {
            case DOWNLOADER_STATUS_PENDING: snprintf(subtitle, sizeof(subtitle), "Queued"); break;
            case DOWNLOADER_STATUS_DOWNLOADING: {
                // Include speed/ETA in subtitle sizing so pill is wide enough
                char speed_str[32], eta_str[32];
                format_download_speed(speed_str, sizeof(speed_str), item->speed_bps);
                format_download_eta(eta_str, sizeof(eta_str), item->eta_sec);
                if (eta_str[0]) {
                    snprintf(subtitle, sizeof(subtitle), "%d%%  %s  ETA %s",
                             item->progress_percent, speed_str, eta_str);
                } else {
                    snprintf(subtitle, sizeof(subtitle), "%d%%  %s",
                             item->progress_percent, speed_str);
                }
                break;
            }
            case DOWNLOADER_STATUS_COMPLETE: snprintf(subtitle, sizeof(subtitle), "Complete"); break;
            case DOWNLOADER_STATUS_FAILED: snprintf(subtitle, sizeof(subtitle), "Failed"); break;
        }

        // Two-row pill (title + subtitle)
        int badge_width = 0;
        // For downloading state, subtitle includes progress bar + gap before text
        int extra_sub_w = (item->status == DOWNLOADER_STATUS_DOWNLOADING) ? SCALE1(50) + SCALE1(6) : 0;
        ListItemBadgedPos pos = render_list_item_pill_badged(screen, &layout, item->title, subtitle, truncated, y, is_selected, badge_width, extra_sub_w);

        // Title text (row 1)
        render_list_item_text(screen, is_selected ? &downloader_queue_scroll_text : NULL,
                              item->title, Fonts_getMedium(),
                              pos.text_x, pos.text_y,
                              pos.text_max_width, is_selected);

        // Subtitle (row 2) — status-dependent
        if (item->status == DOWNLOADER_STATUS_DOWNLOADING) {
            // Progress bar + speed + ETA (like podcast)
            int bar_w = SCALE1(50);
            int bar_h = SCALE1(4);
            int bar_x = pos.subtitle_x;
            int bar_y = pos.subtitle_y + (TTF_FontHeight(Fonts_getSmall()) - bar_h) / 2;

            // Bar background
            SDL_Rect bar_bg = {bar_x, bar_y, bar_w, bar_h};
            SDL_FillRect(screen, &bar_bg, SDL_MapRGB(screen->format, 60, 60, 60));

            // Bar fill
            int fill_w = (bar_w * item->progress_percent) / 100;
            if (fill_w > 0) {
                SDL_Rect bar_fill = {bar_x, bar_y, fill_w, bar_h};
                SDL_FillRect(screen, &bar_fill, THEME_COLOR2);
            }

            // Speed and ETA text
            char info_str[64];
            char speed_str[32];
            char eta_str[32];
            format_download_speed(speed_str, sizeof(speed_str), item->speed_bps);
            format_download_eta(eta_str, sizeof(eta_str), item->eta_sec);

            if (eta_str[0]) {
                snprintf(info_str, sizeof(info_str), "%d%%  %s  ETA %s",
                         item->progress_percent, speed_str, eta_str);
            } else {
                snprintf(info_str, sizeof(info_str), "%d%%  %s",
                         item->progress_percent, speed_str);
            }

            SDL_Surface* info_surf = TTF_RenderUTF8_Blended(Fonts_getSmall(), info_str, COLOR_GRAY);
            if (info_surf) {
                int info_x = bar_x + bar_w + SCALE1(6);
                int avail_w = pos.text_max_width - bar_w - SCALE1(6);
                SDL_Rect src = {0, 0, info_surf->w > avail_w ? avail_w : info_surf->w, info_surf->h};
                SDL_BlitSurface(info_surf, &src, screen, &(SDL_Rect){info_x, pos.subtitle_y});
                SDL_FreeSurface(info_surf);
            }
        } else if (item->status == DOWNLOADER_STATUS_PENDING) {
            SDL_Surface* s = TTF_RenderUTF8_Blended(Fonts_getSmall(), "Queued", COLOR_GRAY);
            if (s) {
                SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                SDL_FreeSurface(s);
            }
        } else if (item->status == DOWNLOADER_STATUS_FAILED) {
            SDL_Surface* s = TTF_RenderUTF8_Blended(Fonts_getSmall(), "Failed", (SDL_Color){200, 80, 80, 255});
            if (s) {
                SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                SDL_FreeSurface(s);
            }
        } else if (item->status == DOWNLOADER_STATUS_COMPLETE) {
            SDL_Surface* s = TTF_RenderUTF8_Blended(Fonts_getSmall(), "Complete", (SDL_Color){80, 200, 80, 255});
            if (s) {
                SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){pos.subtitle_x, pos.subtitle_y});
                SDL_FreeSurface(s);
            }
        }
    }

    // Scroll indicators
    render_scroll_indicators(screen, *queue_scroll, layout.items_per_page, qcount);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (qcount > 0) {
        GFX_blitButtonGroup((char*[]){"X", "REMOVE", "B", "BACK", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }
}

// Check if YouTube results list has active scrolling (for refresh optimization)
bool downloader_results_needs_scroll_refresh(void) {
    return ScrollText_isScrolling(&downloader_results_scroll_text);
}

// Check if results scroll needs a render to transition (delay phase)
bool downloader_results_scroll_needs_render(void) {
    return ScrollText_needsRender(&downloader_results_scroll_text);
}

// Check if YouTube queue list has active scrolling (for refresh optimization)
bool downloader_queue_needs_scroll_refresh(void) {
    return ScrollText_isScrolling(&downloader_queue_scroll_text);
}

// Check if queue scroll needs a render to transition (delay phase)
bool downloader_queue_scroll_needs_render(void) {
    return ScrollText_needsRender(&downloader_queue_scroll_text);
}

// Animate YouTube results scroll only (GPU mode, no screen redraw needed)
void downloader_results_animate_scroll(void) {
    ScrollText_animateOnly(&downloader_results_scroll_text);
}

// Animate YouTube queue scroll only (GPU mode, no screen redraw needed)
void downloader_queue_animate_scroll(void) {
    ScrollText_animateOnly(&downloader_queue_scroll_text);
}

// Clear YouTube queue scroll state (call when queue items are removed)
void downloader_queue_clear_scroll(void) {
    memset(&downloader_queue_scroll_text, 0, sizeof(downloader_queue_scroll_text));
    GFX_clearLayers(LAYER_SCROLLTEXT);
}

// Clear YouTube results scroll state
void downloader_results_clear_scroll(void) {
    memset(&downloader_results_scroll_text, 0, sizeof(downloader_results_scroll_text));
    GFX_clearLayers(LAYER_SCROLLTEXT);
}

void render_ytdlp_updating(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    const DownloaderUpdateStatus* status = Downloader_getUpdateStatus();
    bool installing = strcmp(status->current_version, DOWNLOADER_VERSION_NOT_INSTALLED) == 0;

    render_screen_header(screen,
                         installing ? "Installing Youtube helpers" : "Updating Youtube helpers",
                         show_setting);

    // Current version
    char ver_str[128];
    snprintf(ver_str, sizeof(ver_str), "Current: %s", status->current_version);
    SDL_Surface* ver_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), ver_str, COLOR_GRAY);
    if (ver_text) {
        SDL_BlitSurface(ver_text, NULL, screen, &(SDL_Rect){(hw - ver_text->w) / 2, hh / 2 - SCALE1(50)});
        SDL_FreeSurface(ver_text);
    }

    // Status message
    // The worker names each step as it starts one; the percentage-derived
    // fallbacks only cover the preamble before any step exists, and the
    // terminal states after the last one finishes.
    char status_buf[96];
    const char* status_msg = "Checking connection...";
    if (!status->updating && strlen(status->error_message) > 0) {
        status_msg = status->error_message;
    } else if (!status->updating && !status->update_available && status->progress_percent >= 100) {
        status_msg = "Already up to date!";
    } else if (!status->updating && status->progress_percent >= 100) {
        status_msg = installing ? "Install complete!" : "Update complete!";
    } else if (status->step_message[0] != '\0') {
        if (status->step_count > 1) {
            snprintf(status_buf, sizeof(status_buf), "%s (%d of %d)",
                     status->step_message, status->step_index, status->step_count);
        } else {
            snprintf(status_buf, sizeof(status_buf), "%s", status->step_message);
        }
        status_msg = status_buf;
    } else if (status->progress_percent >= 15) {
        status_msg = "Checking for updates...";
    }

    SDL_Surface* status_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), status_msg, COLOR_WHITE);
    if (status_text) {
        SDL_BlitSurface(status_text, NULL, screen, &(SDL_Rect){(hw - status_text->w) / 2, hh / 2});
        SDL_FreeSurface(status_text);
    }

    // Latest version (if known)
    if (strlen(status->latest_version) > 0) {
        snprintf(ver_str, sizeof(ver_str), "Latest: %s", status->latest_version);
        SDL_Surface* latest_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), ver_str, COLOR_GRAY);
        if (latest_text) {
            SDL_BlitSurface(latest_text, NULL, screen, &(SDL_Rect){(hw - latest_text->w) / 2, hh / 2 + SCALE1(30)});
            SDL_FreeSurface(latest_text);
        }
    }

    // Progress bar
    if (status->updating) {
        int bar_w = hw - SCALE1(PADDING * 8);
        int bar_h = SCALE1(12);
        int bar_x = SCALE1(PADDING * 4);
        int bar_y = hh / 2 + SCALE1(55);

        // Background
        SDL_Rect bg_rect = {bar_x, bar_y, bar_w, bar_h};
        SDL_FillRect(screen, &bg_rect, SDL_MapRGB(screen->format, 64, 64, 64));

        // Progress fill
        int prog_w = (bar_w * status->progress_percent) / 100;
        if (prog_w > 0) {
            SDL_Rect prog_rect = {bar_x, bar_y, prog_w, bar_h};
            SDL_FillRect(screen, &prog_rect, SDL_MapRGB(screen->format, 100, 200, 100));
        }

        // Download detail text
        if (strlen(status->status_detail) > 0) {
            SDL_Surface* detail_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), status->status_detail, COLOR_GRAY);
            if (detail_text) {
                SDL_BlitSurface(detail_text, NULL, screen, &(SDL_Rect){(hw - detail_text->w) / 2, bar_y + bar_h + SCALE1(6)});
                SDL_FreeSurface(detail_text);
            }
        }

        // Percentage text
        char pct_str[16];
        snprintf(pct_str, sizeof(pct_str), "%d%%", status->progress_percent);
        SDL_Surface* pct_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), pct_str, COLOR_WHITE);
        if (pct_text) {
            int pct_x = bar_x + (bar_w - pct_text->w) / 2;
            int pct_y = bar_y + (bar_h - pct_text->h) / 2;
            SDL_BlitSurface(pct_text, NULL, screen, &(SDL_Rect){pct_x, pct_y});
            SDL_FreeSurface(pct_text);
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (status->updating) {
        GFX_blitButtonGroup((char*[]){"B", "CANCEL", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }

    if (!status->updating) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 0, screen, 1);
    }
}
