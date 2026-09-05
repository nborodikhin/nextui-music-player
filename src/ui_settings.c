#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_settings.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "ui_theme.h"
#include "settings.h"
#include "album_art.h"
#include "selfupdate.h"
#include "downloader.h"

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
                          int menu_scroll) {
    GFX_clear(screen);

    int hw = screen->w;

    render_screen_header(screen, "Settings", show_setting);
    ListLayout layout = calc_list_layout(screen);

    if (menu_scroll < 0) menu_scroll = 0;

    char truncated[256];
    char label_buffer[256];

    for (int row = 0; row < layout.items_per_page && menu_scroll + row < SETTINGS_ITEM_COUNT; row++) {
        int i = menu_scroll + row;
        bool selected = (i == menu_selected);

        int item_y = layout.list_y + row * layout.item_h;

        // Build label text based on item
        const char* label = "";
        const char* value_str = NULL;

        switch (i) {
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
            case SETTINGS_ITEM_AUTO_UPDATE:
                label = "Auto Update Check";
                value_str = Settings_getAutoUpdateDisplayStr();
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
                label = Downloader_isAvailable()
                            ? "Update Youtube download helpers"
                            : "Install Youtube download helpers";
                break;
            case SETTINGS_ITEM_ABOUT: {
                const SelfUpdateStatus status = SelfUpdate_getStatus();
                if (status.update_available) {
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

        int text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
        int text_y = item_y + (SCALE1(PILL_SIZE) - TTF_FontHeight(font)) / 2;

        // draw band
        if (selected && value_str) {
            int row_width = hw - SCALE1(PADDING * 2);
            SDL_Rect row_rect = {SCALE1(PADDING), item_y, row_width, SCALE1(PILL_SIZE)};
            draw_list_item_band(screen, &row_rect);
        }

        // draw value
        if (value_str) {
            char value_to_print[64];
            if (selected) {
                snprintf(value_to_print, sizeof(value_to_print), "< %s >", value_str);
            } else {
                snprintf(value_to_print, sizeof(value_to_print), "%s", value_str);
            }

            // value is always drawn on the non-selected part of the line
            SDL_Color value_color = Theme_getColor(THEME_ROLE_PRIMARY, false);
            SDL_Surface* val_surf = TTF_RenderUTF8_Blended(font, value_to_print, value_color);
            if (val_surf) {
                int value_x = hw - SCALE1(PADDING) - SCALE1(BUTTON_PADDING) - val_surf->w;
                SDL_BlitSurface(val_surf, NULL, screen, &(SDL_Rect){value_x, text_y, 0, 0});
                SDL_FreeSurface(val_surf);
            }
        }

        // draw label bg
        SDL_Rect label_pill_rect = {SCALE1(PADDING), item_y, label_pill_width, SCALE1(PILL_SIZE)};
        draw_list_item_bg(screen, &label_pill_rect, selected);

        // draw label
        SDL_Color label_color = Theme_getColor(THEME_ROLE_PRIMARY, selected);
        SDL_Surface* label_surf = TTF_RenderUTF8_Blended(font, label, label_color);
        if (label_surf) {
            SDL_BlitSurface(label_surf, NULL, screen, &(SDL_Rect){text_x, text_y, 0, 0});
            SDL_FreeSurface(label_surf);
        }
    }

    // Scroll indicators when the list overflows the visible window
    render_scroll_indicators(screen, menu_scroll, layout.items_per_page, SETTINGS_ITEM_COUNT);

    // Button hints
    GFX_blitButtonGroup((char *[]){"START", "CONTROLS", NULL}, 0, screen, 0);

    // Different hints based on selected item
    if (menu_selected == SETTINGS_ITEM_SCREEN_OFF ||
        menu_selected == SETTINGS_ITEM_BASS_FILTER ||
        menu_selected == SETTINGS_ITEM_SOFT_LIMITER ||
        menu_selected == SETTINGS_ITEM_AUTO_UPDATE) {
        GFX_blitButtonGroup((char *[]){"B", "BACK", "LEFT/RIGHT", "CHANGE", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char *[]){"B", "BACK", "A", "OPEN", NULL}, 1, screen, 1);
    }
}
