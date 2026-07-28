#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_settings.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "settings.h"
#include "album_art.h"
#include "selfupdate.h"
#include "downloader.h"

int settings_build_visible_items(int* out, int max_count, bool has_crash_bundles) {
    int n = 0;
    if (n < max_count) out[n++] = SETTINGS_ITEM_SCREEN_OFF;
    if (n < max_count) out[n++] = SETTINGS_ITEM_BASS_FILTER;
    if (n < max_count) out[n++] = SETTINGS_ITEM_SOFT_LIMITER;
    if (n < max_count) out[n++] = SETTINGS_ITEM_COLLECT_CRASH;
    if (has_crash_bundles) {
        if (n < max_count) out[n++] = SETTINGS_ITEM_DELETE_CRASH;
    }
    if (n < max_count) out[n++] = SETTINGS_ITEM_CLEAR_CACHE;
    if (n < max_count) out[n++] = SETTINGS_ITEM_UPDATE_YTDLP;
    if (n < max_count) out[n++] = SETTINGS_ITEM_ABOUT;
    return n;
}

// Format cache size as human-readable string
static void format_cache_size(long bytes, char* buf, int buf_size) {
    if (bytes >= 1024 * 1024) {
        snprintf(buf, buf_size, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, buf_size, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buf, buf_size, "%ld B", bytes);
    }
}

void render_settings_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                          int menu_scroll, bool has_crash_bundles) {
    GFX_clear(screen);

    int hw = screen->w;

    render_screen_header(screen, "Settings", show_setting);
    ListLayout layout = calc_list_layout(screen);

    int item_h = layout.item_h;
    int start_y = layout.list_y;

    int visible_items[SETTINGS_VISIBLE_MAX];
    int visible_count = settings_build_visible_items(visible_items, SETTINGS_VISIBLE_MAX,
                                                     has_crash_bundles);

    // Clamp scroll to valid range (defensive — caller should already adjust)
    int max_scroll = (visible_count > layout.items_per_page)
                     ? visible_count - layout.items_per_page : 0;
    if (menu_scroll < 0) menu_scroll = 0;
    if (menu_scroll > max_scroll) menu_scroll = max_scroll;

    char truncated[256];
    char label_buffer[256];

    int last_visible = menu_scroll + layout.items_per_page;
    if (last_visible > visible_count) last_visible = visible_count;

    int selected_item_id = (menu_selected >= 0 && menu_selected < visible_count)
                           ? visible_items[menu_selected] : -1;

    for (int row = menu_scroll; row < last_visible; row++) {
        bool selected = (row == menu_selected);
        int item_id = visible_items[row];

        int item_y = start_y + (row - menu_scroll) * item_h;

        // Build label text based on item
        const char* label = "";
        const char* value_str = NULL;

        switch (item_id) {
            case SETTINGS_ITEM_SCREEN_OFF:
                label = "Auto Screen Off";
                value_str = Settings_getScreenOffDisplayStr();
                break;
            case SETTINGS_ITEM_BASS_FILTER:
                label = "Bass Filter";
                value_str = Settings_getBassFilterDisplayStr();
                break;
            case SETTINGS_ITEM_SOFT_LIMITER:
                label = "Soft Limiter";
                value_str = Settings_getSoftLimiterDisplayStr();
                break;
            case SETTINGS_ITEM_COLLECT_CRASH:
                label = "Collect Crash Reports";
                value_str = Settings_getCollectCrashReportsDisplayStr();
                break;
            case SETTINGS_ITEM_DELETE_CRASH:
                label = "Delete Crash Reports";
                break;
            case SETTINGS_ITEM_CLEAR_CACHE: {
                long cache_size = album_art_get_cache_size();
                char size_str[32];
                format_cache_size(cache_size, size_str, sizeof(size_str));
                snprintf(label_buffer, sizeof(label_buffer), "Clear Album Art (%s)", size_str);
                label = label_buffer;
                break;
            }
            case SETTINGS_ITEM_UPDATE_YTDLP:
                label = "Update yt-dlp";
                break;
            case SETTINGS_ITEM_ABOUT: {
                const SelfUpdateStatus* status = SelfUpdate_getStatus();
                if (status->update_available) {
                    label = "About (Update available)";
                } else {
                    label = "About";
                }
                break;
            }
        }

        // Use medium font for settings menu
        TTF_Font* font = Fonts_getMedium();

        // Measure label text
        int text_w, text_h;
        TTF_SizeUTF8(font, label, &text_w, &text_h);
        int label_pill_width = text_w + SCALE1(BUTTON_PADDING * 2);

        // Text position
        int text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
        int text_y = item_y + (SCALE1(PILL_SIZE) - TTF_FontHeight(font)) / 2;

        if (selected) {
            // Selected item rendering - use theme colors
            SDL_Color selected_text_color = Fonts_getListTextColor(true);

            if (value_str) {
                // Item with option value: 2-layer approach
                // Layer 1: Primary accent color for full-width row background (options area)
                // Layer 2: Main/white pill around just the label on top

                // 1. Draw full-width pill as row background with primary accent color
                int row_width = hw - SCALE1(PADDING * 2);
                SDL_Rect row_rect = {SCALE1(PADDING), item_y, row_width, SCALE1(PILL_SIZE)};
                GFX_blitPillColor(ASSET_WHITE_PILL, screen, &row_rect, THEME_COLOR2, RGB_WHITE);

                // 2. Draw THEME_COLOR2 pill around just the label (on top)
                SDL_Rect label_pill_rect = {SCALE1(PADDING), item_y, label_pill_width, SCALE1(PILL_SIZE)};
                GFX_blitPillColor(ASSET_WHITE_PILL, screen, &label_pill_rect, THEME_COLOR1, RGB_WHITE);

                // 3. Render label with selected text color (dark on white pill)
                SDL_Surface* label_surf = TTF_RenderUTF8_Blended(font, label, selected_text_color);
                if (label_surf) {
                    SDL_BlitSurface(label_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
                    SDL_FreeSurface(label_surf);
                }

                // 4. Render value with arrows in white (on accent background)
                int value_x = hw - SCALE1(PADDING) - SCALE1(BUTTON_PADDING);
                char value_with_arrows[64];
                snprintf(value_with_arrows, sizeof(value_with_arrows), "< %s >", value_str);
                SDL_Surface* val_surf = TTF_RenderUTF8_Blended(font, value_with_arrows, COLOR_WHITE);
                if (val_surf) {
                    value_x -= val_surf->w;
                    SDL_BlitSurface(val_surf, NULL, screen, &(SDL_Rect){value_x, text_y, 0, 0});
                    SDL_FreeSurface(val_surf);
                }
            } else {
                // Item without option: pill with primary accent color
                SDL_Rect label_pill_rect = {SCALE1(PADDING), item_y, label_pill_width, SCALE1(PILL_SIZE)};
                GFX_blitPillColor(ASSET_WHITE_PILL, screen, &label_pill_rect, THEME_COLOR1, RGB_WHITE);

                // Render label with selected text color
                SDL_Surface* label_surf = TTF_RenderUTF8_Blended(font, label, selected_text_color);
                if (label_surf) {
                    SDL_BlitSurface(label_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
                    SDL_FreeSurface(label_surf);
                }
            }
        } else {
            // Unselected item: no background, theme-aware text color
            SDL_Color text_color = Fonts_getListTextColor(false);

            // Render label
            SDL_Surface* label_surf = TTF_RenderUTF8_Blended(font, label, text_color);
            if (label_surf) {
                SDL_BlitSurface(label_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
                SDL_FreeSurface(label_surf);
            }

            // Render value
            if (value_str) {
                int value_x = hw - SCALE1(PADDING) - SCALE1(BUTTON_PADDING);
                SDL_Surface* val_surf = TTF_RenderUTF8_Blended(font, value_str, text_color);
                if (val_surf) {
                    value_x -= val_surf->w;
                    SDL_BlitSurface(val_surf, NULL, screen, &(SDL_Rect){value_x, text_y, 0, 0});
                    SDL_FreeSurface(val_surf);
                }
            }
        }
    }

    // Scroll indicators (... at top/bottom when content overflows the visible window)
    render_scroll_indicators(screen, menu_scroll, layout.items_per_page, visible_count);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);

    // Different hints based on selected item
    if (selected_item_id == SETTINGS_ITEM_SCREEN_OFF ||
        selected_item_id == SETTINGS_ITEM_BASS_FILTER ||
        selected_item_id == SETTINGS_ITEM_SOFT_LIMITER ||
        selected_item_id == SETTINGS_ITEM_COLLECT_CRASH) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "LEFT/RIGHT", "CHANGE", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "OPEN", NULL}, 1, screen, 1);
    }
}

void render_ytdlp_updating(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "Updating yt-dlp", show_setting);

    const DownloaderUpdateStatus* status = Downloader_getUpdateStatus();

    // Current version
    char ver_str[128];
    snprintf(ver_str, sizeof(ver_str), "Current: %s", status->current_version);
    SDL_Surface* ver_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), ver_str, COLOR_GRAY);
    if (ver_text) {
        SDL_BlitSurface(ver_text, NULL, screen, &(SDL_Rect){(hw - ver_text->w) / 2, hh / 2 - SCALE1(50)});
        SDL_FreeSurface(ver_text);
    }

    // Status message
    const char* status_msg = "Checking connection...";
    if (status->progress_percent >= 15 && status->progress_percent < 30) {
        status_msg = "Fetching version info...";
    } else if (status->progress_percent >= 30 && status->progress_percent < 50) {
        status_msg = "Checking for updates...";
    } else if (status->progress_percent >= 50 && status->progress_percent < 80) {
        status_msg = "Downloading yt-dlp...";
    } else if (status->progress_percent >= 80 && status->progress_percent < 100) {
        status_msg = "Installing update...";
    } else if (!status->updating && !status->update_available && status->progress_percent >= 100) {
        status_msg = "Already up to date!";
    } else if (status->progress_percent >= 100) {
        status_msg = "Update complete!";
    } else if (!status->updating && strlen(status->error_message) > 0) {
        status_msg = status->error_message;
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
}

