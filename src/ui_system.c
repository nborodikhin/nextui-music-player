#include <stdio.h>
#include <string.h>

#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "ui_system.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "selfupdate.h"
#include "qr_code_data.h"

// Render the app update screen
void render_app_updating(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "App Update", show_setting);

    const SelfUpdateStatus* status = SelfUpdate_getStatus();
    SelfUpdateState state = status->state;

    // Version info: "v0.1.0 → v0.2.0"
    char ver_str[128];
    if (strlen(status->latest_version) > 0) {
        snprintf(ver_str, sizeof(ver_str), "%s  ->  %s", status->current_version, status->latest_version);
    } else {
        snprintf(ver_str, sizeof(ver_str), "%s", status->current_version);
    }
    int ver_y = SCALE1(PADDING * 3 + 35);
    SDL_Surface* ver_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), ver_str, COLOR_GRAY);
    if (ver_text) {
        SDL_BlitSurface(ver_text, NULL, screen, &(SDL_Rect){(hw - ver_text->w) / 2, ver_y});
        SDL_FreeSurface(ver_text);
    }

    // Release notes area with word wrapping (positioned right below version info)
    int notes_y = ver_y + SCALE1(30);
    int notes_max_lines = 5;
    int line_height = SCALE1(18);
    int max_line_width = hw - SCALE1(PADDING * 6);

    if (strlen(status->release_notes) > 0 && state != SELFUPDATE_STATE_CHECKING) {
        // Word-wrap release notes
        char notes_copy[1024];
        strncpy(notes_copy, status->release_notes, sizeof(notes_copy) - 1);
        notes_copy[sizeof(notes_copy) - 1] = '\0';

        // Replace newlines with spaces for continuous wrapping
        for (int i = 0; notes_copy[i]; i++) {
            if (notes_copy[i] == '\n' || notes_copy[i] == '\r') notes_copy[i] = ' ';
        }

        char wrapped_lines[5][128];
        int line_count = 0;
        char* src = notes_copy;

        while (*src && line_count < notes_max_lines) {
            // Skip leading spaces
            while (*src == ' ') src++;
            if (!*src) break;

            // Find how many characters fit in max_line_width
            char test_line[128] = "";
            int char_count = 0;
            int last_space = -1;

            while (src[char_count] && char_count < 127) {
                test_line[char_count] = src[char_count];
                test_line[char_count + 1] = '\0';

                if (src[char_count] == ' ') last_space = char_count;

                // Check width
                int text_w, text_h;
                TTF_SizeUTF8(Fonts_getSmall(), test_line, &text_w, &text_h);
                if (text_w > max_line_width) {
                    // Line too long, break at last space or current position
                    if (last_space > 0) {
                        char_count = last_space;
                    }
                    break;
                }
                char_count++;
            }

            // Copy the line
            strncpy(wrapped_lines[line_count], src, char_count);
            wrapped_lines[line_count][char_count] = '\0';
            src += char_count;
            line_count++;
        }

        // Render wrapped lines
        for (int i = 0; i < line_count; i++) {
            if (strlen(wrapped_lines[i]) > 0) {
                SDL_Surface* line_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), wrapped_lines[i], COLOR_WHITE);
                if (line_text) {
                    SDL_BlitSurface(line_text, NULL, screen, &(SDL_Rect){(hw - line_text->w) / 2, notes_y + i * line_height});
                    SDL_FreeSurface(line_text);
                }
            }
        }
    } else if (state == SELFUPDATE_STATE_CHECKING) {
        // Show checking message
        SDL_Surface* check_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), "Checking for updates...", COLOR_GRAY);
        if (check_text) {
            SDL_BlitSurface(check_text, NULL, screen, &(SDL_Rect){(hw - check_text->w) / 2, notes_y});
            SDL_FreeSurface(check_text);
        }
    }

    // Progress bar (only during active update) - positioned higher on screen
    if (state == SELFUPDATE_STATE_DOWNLOADING || state == SELFUPDATE_STATE_EXTRACTING ||
        state == SELFUPDATE_STATE_APPLYING) {
        int bar_w = hw - SCALE1(PADDING * 8);
        int bar_h = SCALE1(12);
        int bar_x = SCALE1(PADDING * 4);
        int bar_y = hh - SCALE1(PILL_SIZE + PADDING * 10);

        // Background
        SDL_Rect bg_rect = {bar_x, bar_y, bar_w, bar_h};
        SDL_FillRect(screen, &bg_rect, SDL_MapRGB(screen->format, 64, 64, 64));

        // Progress fill
        int prog_w = (bar_w * status->progress_percent) / 100;
        if (prog_w > 0) {
            SDL_Rect prog_rect = {bar_x, bar_y, prog_w, bar_h};
            SDL_FillRect(screen, &prog_rect, SDL_MapRGB(screen->format, 100, 200, 100));
        }

        // Percentage text inside bar
        char pct_str[16];
        snprintf(pct_str, sizeof(pct_str), "%d%%", status->progress_percent);
        SDL_Surface* pct_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), pct_str, COLOR_WHITE);
        if (pct_text) {
            int pct_x = bar_x + (bar_w - pct_text->w) / 2;
            int pct_y = bar_y + (bar_h - pct_text->h) / 2;
            SDL_BlitSurface(pct_text, NULL, screen, &(SDL_Rect){pct_x, pct_y});
            SDL_FreeSurface(pct_text);
        }

        // Download size detail (e.g., "2.5 MB / 5.0 MB") - below progress bar
        if (strlen(status->status_detail) > 0) {
            SDL_Surface* detail_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), status->status_detail, COLOR_GRAY);
            if (detail_text) {
                SDL_BlitSurface(detail_text, NULL, screen, &(SDL_Rect){(hw - detail_text->w) / 2, bar_y + bar_h + SCALE1(6)});
                SDL_FreeSurface(detail_text);
            }
        }
    }

    // Status message during active operations - but not during downloading (size detail is shown instead)
    if (state == SELFUPDATE_STATE_EXTRACTING || state == SELFUPDATE_STATE_APPLYING ||
        state == SELFUPDATE_STATE_COMPLETED || state == SELFUPDATE_STATE_ERROR) {

        const char* status_msg = status->status_message;
        if (state == SELFUPDATE_STATE_ERROR && strlen(status->error_message) > 0) {
            status_msg = status->error_message;
        }

        SDL_Color status_color = COLOR_WHITE;
        if (state == SELFUPDATE_STATE_ERROR) {
            status_color = (SDL_Color){255, 100, 100, 255};
        } else if (state == SELFUPDATE_STATE_COMPLETED) {
            status_color = (SDL_Color){100, 255, 100, 255};
        }

        SDL_Surface* status_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), status_msg, status_color);
        if (status_text) {
            SDL_BlitSurface(status_text, NULL, screen, &(SDL_Rect){(hw - status_text->w) / 2, hh - SCALE1(PILL_SIZE + PADDING * 6)});
            SDL_FreeSurface(status_text);
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (state == SELFUPDATE_STATE_COMPLETED) {
        GFX_blitButtonGroup((char*[]){"A", "RESTART", NULL}, 1, screen, 1);
    } else if (state == SELFUPDATE_STATE_DOWNLOADING) {
        GFX_blitButtonGroup((char*[]){"B", "CANCEL", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }
}

// Render the about screen
void render_about(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "About", show_setting);

    // App name
    const char* app_name = "Music Player";
    SDL_Surface* name_text = TTF_RenderUTF8_Blended(Fonts_getLarge(), app_name, COLOR_WHITE);
    if (name_text) {
        SDL_BlitSurface(name_text, NULL, screen, &(SDL_Rect){(hw - name_text->w) / 2, SCALE1(PADDING + PILL_SIZE)});
        SDL_FreeSurface(name_text);
    }

    // Tagline (2 lines)
    int info_y = SCALE1(PADDING + PILL_SIZE + 30);
    const char* tagline1 = "Your favorite tunes on the go,";
    const char* tagline2 = "powered by your gaming handheld.";
    SDL_Surface* tagline_text1 = TTF_RenderUTF8_Blended(Fonts_getSmall(), tagline1, COLOR_WHITE);
    if (tagline_text1) {
        SDL_BlitSurface(tagline_text1, NULL, screen, &(SDL_Rect){(hw - tagline_text1->w) / 2, info_y});
        SDL_FreeSurface(tagline_text1);
    }
    SDL_Surface* tagline_text2 = TTF_RenderUTF8_Blended(Fonts_getSmall(), tagline2, COLOR_WHITE);
    if (tagline_text2) {
        SDL_BlitSurface(tagline_text2, NULL, screen, &(SDL_Rect){(hw - tagline_text2->w) / 2, info_y + SCALE1(18)});
        SDL_FreeSurface(tagline_text2);
    }

    // GitHub QR Code
    SDL_RWops* rw = SDL_RWFromConstMem(qr_code_png, qr_code_png_len);
    if (rw) {
        SDL_Surface* qr_surface = IMG_Load_RW(rw, 1);
        if (qr_surface) {
            // Scale QR code to fit nicely (target ~150x150 pixels)
            int qr_size = SCALE1(75);
            SDL_Rect src_rect = {0, 0, qr_surface->w, qr_surface->h};
            SDL_Rect dst_rect = {(hw - qr_size) / 2, hh - SCALE1(PILL_SIZE + PADDING * 4) - qr_size, qr_size, qr_size};
            SDL_BlitScaled(qr_surface, &src_rect, screen, &dst_rect);
            SDL_FreeSurface(qr_surface);
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
}
