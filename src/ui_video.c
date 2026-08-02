#include <stdio.h>
#include <string.h>
#include <time.h>

#include "defines.h"
#include "api.h"
#include "ui_video.h"
#include "ui_fonts.h"
#include "ui_main.h"
#include "ui_icons.h"
#include "ui_utils.h"
#include "settings.h"

// Scroll state for video browser
static ScrollTextState video_browser_scroll = {0};

void render_video_browser(SDL_Surface* screen, int show_setting, BrowserContext* browser) {
    GFX_clear(screen);

    render_screen_header(screen, "Videos", show_setting);

    // Empty state at root
    if (browser->entry_count == 0 || (browser->entry_count == 1 && browser->entries[0].is_play_all)) {
        render_empty_state(screen, "No videos found", "Add videos to /Videos on your SD card", NULL);
        return;
    }

    ListLayout layout = calc_list_layout(screen);
    browser->items_per_page = layout.items_per_page;

    adjust_list_scroll(browser->selected, &browser->scroll_offset, browser->items_per_page);

    int icon_size = Icons_isLoaded() ? SCALE1(24) : 0;
    int icon_spacing = Icons_isLoaded() ? SCALE1(6) : 0;
    int icon_offset = icon_size + icon_spacing;
    char truncated[256];

    for (int i = 0; i < browser->items_per_page && browser->scroll_offset + i < browser->entry_count; i++) {
        int idx = browser->scroll_offset + i;
        FileEntry* entry = &browser->entries[idx];
        bool selected = (idx == browser->selected);
        int y = layout.list_y + i * layout.item_h;

        char display[256];
        if (entry->is_dir || entry->is_play_all) {
            strncpy(display, entry->name, sizeof(display) - 1);
            display[sizeof(display) - 1] = '\0';
        } else {
            Browser_getDisplayName(entry->name, display, sizeof(display));
        }

        ListItemPos pos = render_list_item_pill(screen, &layout, display, truncated, y, selected, icon_offset);

        if (Icons_isLoaded()) {
            SDL_Surface* icon = NULL;
            if (entry->is_dir) {
                icon = Icons_getFolder(selected);
            } else if (entry->is_play_all) {
                icon = Icons_getPlayAll(selected);
            } else {
                icon = Icons_getAudio(selected);
            }
            if (icon) {
                int icon_y = y + (layout.item_h - icon_size) / 2;
                int icon_x = pos.text_x;
                SDL_Rect src_rect = {0, 0, icon->w, icon->h};
                SDL_Rect dst_rect = {icon_x, icon_y, icon_size, icon_size};
                SDL_BlitScaled(icon, &src_rect, screen, &dst_rect);
            }
        }

        int text_x = pos.text_x + icon_offset;
        int available_width = pos.pill_width - SCALE1(BUTTON_PADDING * 2) - icon_offset;

        render_list_item_text(screen, &video_browser_scroll, display, Fonts_getMedium(),
                              text_x, pos.text_y, available_width, selected);
    }

    render_scroll_indicators(screen, browser->scroll_offset, browser->items_per_page, browser->entry_count);

    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "X", "AUDIO ONLY", "A", "PLAY", NULL}, 1, screen, 1);
}

static void format_time_str(int seconds, char* buf, size_t buf_size) {
    if (seconds < 0) seconds = 0;
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        snprintf(buf, buf_size, "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(buf, buf_size, "%02d:%02d", m, s);
    }
}

void render_video_osd(SDL_Surface* screen, const char* title, int current_seconds, int total_seconds,
                      bool is_paused, bool is_locked, int show_setting, int seek_offset,
                      bool show_hud) {
    if (is_locked) {
        render_video_lockscreen(screen);
        return;
    }

    if (!show_hud && !is_paused && seek_offset == 0) {
        return;
    }

    int sw = screen->w;
    int sh = screen->h;

    // Top Bar Pill (Title)
    int bar_pad = SCALE1(12);
    int top_h = SCALE1(36);
    SDL_Rect top_bar = {bar_pad, bar_pad, sw - bar_pad * 2, top_h};
    GFX_blitPill(ASSET_BLACK_PILL, screen, &top_bar);

    // Render Title
    TTF_Font* font_med = Fonts_getMedium();
    if (font_med && title) {
        SDL_Surface* title_surf = TTF_RenderUTF8_Blended(font_med, title, COLOR_WHITE);
        if (title_surf) {
            int tx = top_bar.x + SCALE1(12);
            int ty = top_bar.y + (top_h - title_surf->h) / 2;
            SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){tx, ty});
            SDL_FreeSurface(title_surf);
        }
    }

    // Center seek / pause indicator if active
    if (seek_offset != 0) {
        char seek_text[32];
        if (seek_offset > 0) {
            snprintf(seek_text, sizeof(seek_text), "+%d s", seek_offset);
        } else {
            snprintf(seek_text, sizeof(seek_text), "%d s", seek_offset);
        }
        SDL_Surface* st = TTF_RenderUTF8_Blended(Fonts_getLarge(), seek_text, COLOR_WHITE);
        if (st) {
            int pill_w = st->w + SCALE1(24);
            int pill_h = st->h + SCALE1(16);
            SDL_Rect seek_rect = {(sw - pill_w) / 2, (sh - pill_h) / 2, pill_w, pill_h};
            GFX_blitPill(ASSET_BLACK_PILL, screen, &seek_rect);
            SDL_BlitSurface(st, NULL, screen, &(SDL_Rect){seek_rect.x + SCALE1(12), seek_rect.y + SCALE1(8)});
            SDL_FreeSurface(st);
        }
    } else if (is_paused) {
        SDL_Surface* pt = TTF_RenderUTF8_Blended(Fonts_getLarge(), "PAUSED", COLOR_WHITE);
        if (pt) {
            int pill_w = pt->w + SCALE1(24);
            int pill_h = pt->h + SCALE1(16);
            SDL_Rect pause_rect = {(sw - pill_w) / 2, (sh - pill_h) / 2, pill_w, pill_h};
            GFX_blitPill(ASSET_BLACK_PILL, screen, &pause_rect);
            SDL_BlitSurface(pt, NULL, screen, &(SDL_Rect){pause_rect.x + SCALE1(12), pause_rect.y + SCALE1(8)});
            SDL_FreeSurface(pt);
        }
    }

    // Bottom Progress Bar Pill
    int bot_h = SCALE1(40);
    int bot_y = sh - bot_h - bar_pad;
    SDL_Rect bot_bar = {bar_pad, bot_y, sw - bar_pad * 2, bot_h};
    GFX_blitPill(ASSET_BLACK_PILL, screen, &bot_bar);

    char cur_str[32], dur_str[32];
    format_time_str(current_seconds, cur_str, sizeof(cur_str));
    format_time_str(total_seconds, dur_str, sizeof(dur_str));

    TTF_Font* font_sm = Fonts_getSmall();
    int cur_w = 0, dur_w = 0;
    TTF_SizeUTF8(font_sm, cur_str, &cur_w, NULL);
    TTF_SizeUTF8(font_sm, dur_str, &dur_w, NULL);

    SDL_Surface* cur_surf = TTF_RenderUTF8_Blended(font_sm, cur_str, COLOR_GRAY);
    if (cur_surf) {
        SDL_BlitSurface(cur_surf, NULL, screen, &(SDL_Rect){bot_bar.x + SCALE1(12), bot_bar.y + (bot_h - cur_surf->h) / 2});
        SDL_FreeSurface(cur_surf);
    }

    SDL_Surface* dur_surf = TTF_RenderUTF8_Blended(font_sm, dur_str, COLOR_GRAY);
    if (dur_surf) {
        SDL_BlitSurface(dur_surf, NULL, screen, &(SDL_Rect){bot_bar.x + bot_bar.w - dur_w - SCALE1(12), bot_bar.y + (bot_h - dur_surf->h) / 2});
        SDL_FreeSurface(dur_surf);
    }

    // Progress line
    int bar_x = bot_bar.x + SCALE1(16) + cur_w + SCALE1(8);
    int bar_max_w = (bot_bar.x + bot_bar.w - dur_w - SCALE1(24)) - bar_x;
    if (bar_max_w > SCALE1(20)) {
        int track_h = SCALE1(4);
        int track_y = bot_bar.y + (bot_h - track_h) / 2;
        SDL_Rect bg_track = {bar_x, track_y, bar_max_w, track_h};
        SDL_FillRect(screen, &bg_track, SDL_MapRGB(screen->format, 60, 60, 60));

        if (total_seconds > 0) {
            float progress = (float)current_seconds / (float)total_seconds;
            if (progress > 1.0f) progress = 1.0f;
            if (progress < 0.0f) progress = 0.0f;
            int fill_w = (int)(bar_max_w * progress);
            if (fill_w < track_h) fill_w = track_h;
            SDL_Rect fill_track = {bar_x, track_y, fill_w, track_h};
            SDL_FillRect(screen, &fill_track, SDL_MapRGB(screen->format, 255, 255, 255));
        }
    }
}

void render_video_lockscreen(SDL_Surface* screen) {
    GFX_clear(screen);

    int sw = screen->w;
    int sh = screen->h;

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M", tm);

    SDL_Surface* time_surf = TTF_RenderUTF8_Blended(Fonts_getTitle(), time_buf, COLOR_WHITE);
    SDL_Surface* msg_surf = TTF_RenderUTF8_Blended(Fonts_getSmall(), "Audio Playing • Press A to unlock", COLOR_GRAY);

    int total_h = (time_surf ? time_surf->h : 0) + SCALE1(12) + (msg_surf ? msg_surf->h : 0);
    int cur_y = (sh - total_h) / 2;

    if (time_surf) {
        SDL_BlitSurface(time_surf, NULL, screen, &(SDL_Rect){(sw - time_surf->w) / 2, cur_y});
        cur_y += time_surf->h + SCALE1(12);
        SDL_FreeSurface(time_surf);
    }
    if (msg_surf) {
        SDL_BlitSurface(msg_surf, NULL, screen, &(SDL_Rect){(sw - msg_surf->w) / 2, cur_y});
        SDL_FreeSurface(msg_surf);
    }
}
