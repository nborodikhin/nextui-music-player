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
                label = Downloader_isAvailable() ? "Update Youtube download helpers"
                                                : "Install Youtube download helpers";
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

    // Scroll indicators when the list overflows the visible window
    render_scroll_indicators(screen, menu_scroll, layout.items_per_page, SETTINGS_ITEM_COUNT);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);

    // Different hints based on selected item
    if (menu_selected == SETTINGS_ITEM_SCREEN_OFF ||
        menu_selected == SETTINGS_ITEM_BASS_FILTER ||
        menu_selected == SETTINGS_ITEM_SOFT_LIMITER ||
        menu_selected == SETTINGS_ITEM_AUTO_UPDATE) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "LEFT/RIGHT", "CHANGE", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "OPEN", NULL}, 1, screen, 1);
    }
}


