#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_radio_playing.h"
#include "radio.h"
#include "spectrum.h"
#include "ui_fonts.h"
#include "ui_layers.h"
#include "ui_utils.h"
#include "ui_theme.h"
#include "ui_album_art.h"
#include "album_art.h"

// True while a station waits for its screen, before `Radio_play()` runs. The
// module of the radio sets it, because only the module knows.
static bool radio_ui_waiting_to_start = false;

// The row of the state at the foot of the screen. The render records where
// the row goes, and the painters of the layers draw there.
static int  status_x = 0, status_y = 0;
static bool status_position_set = false;

// The buffer bar: its track on the surface, and its fill on the animation
// layer. `track_shown` says whether the last render drew the track, thus the
// frame that changes that asks for a render.
static SDL_Rect track = {0};
static bool     track_shown = false;
static int      fill_shown_w = -1;

// The row of the state, ready to draw. The painter of the status layer blits
// it, thus the build of it happens only where a value of the row changes.
static SDL_Surface* status_surface = NULL;

// What the cached surface holds. The build of the surface skips its work where
// each of the three is the same as the last time, thus these go with the
// surface and never outlive it.
static RadioState status_last_state = RADIO_STATE_STOPPED;
static int        status_last_bitrate = 0;
static bool       status_last_waiting = false;
static bool       status_last_low_buffer = false;
static bool       status_holds_row = false;

// Set where the display lost the layers, thus the next frame paints each one
static bool layers_stale = false;

void RadioUI_setWaitingToStart(bool waiting) {
    radio_ui_waiting_to_start = waiting;
}

// The bar of the buffer says how much sound is ready to play. A station that
// does not play has no such sound: a station that connects has not started,
// and one that the user stopped keeps nothing. The bar therefore draws while
// the sound plays, and while it stalls with the state that says so.
static bool sound_is_running(void) {
    RadioState state = Radio_getState();
    return !radio_ui_waiting_to_start &&
           (state == RADIO_STATE_PLAYING || state == RADIO_STATE_BUFFERING);
}

static RadioStation* get_station_by_index(int index) {
    RadioStation* stations;
    int count = Radio_getStations(&stations);
    if (count > 0 && index < count) {
        return &stations[index];
    }
    return NULL;
}

// Render the radio playing screen
void render_radio_playing(SDL_Surface* screen, int show_setting, int radio_selected) {
    GFX_clear(screen);

    // Render album art as triangular background (if available and not being fetched)
    // Skip during fetch to avoid accessing potentially invalid surface
    if (!album_art_is_fetching()) {
        SDL_Surface* album_art = Radio_getAlbumArt();
        if (album_art && album_art->w > 0 && album_art->h > 0) {
            render_album_art_background(screen, album_art);
        }
    }

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    RadioState state = Radio_getState();
    const RadioMetadata* meta = Radio_getMetadata();
    RadioStation* current_station = get_station_by_index(radio_selected);
    RadioStation* stations;
    int station_count = Radio_getStations(&stations);

    // === TOP BAR ===
    int top_y = top_of_the_chip_box(screen, chip_height());

    // Source chip
    const char* chip_text = "RADIO";
    SDL_Rect chip = draw_chip(screen, chip_text, SCALE1(PADDING), top_y);

    // Station counter "01 - 12" (like track counter in local player)
    char station_str[32];
    snprintf(station_str, sizeof(station_str), "%02d - %02d", radio_selected + 1, station_count);
    SDL_Surface* station_surf = TTF_RenderUTF8_Blended(
        Fonts_getTiny(), station_str, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (station_surf) {
        int station_x = chip.x + chip.w + SCALE1(8);
        int station_y = top_y + (chip.h - station_surf->h) / 2;
        SDL_BlitSurface(station_surf, NULL, screen, &(SDL_Rect){station_x, station_y});
        SDL_FreeSurface(station_surf);
    }

    // Hardware status (clock, battery) on right
    if (screen_has_status_group(screen)) GFX_blitHardwareGroup(screen, show_setting);

    // === STATION INFO SECTION ===
    int info_y = total_header_height(screen, chip_height());

    // Max widths for text (album art is now only shown as background)
    int max_w_half = (hw - SCALE1(PADDING * 2)) / 2;
    int max_w_full = hw - SCALE1(PADDING * 2);

    // Genre (like Artist in local player) - gray, medium font
    const char* genre = (current_station && current_station->genre[0]) ? current_station->genre : "Radio";
    GFX_truncateText(Fonts_getArtist(), genre, truncated, max_w_half, 0);
    SDL_Surface* genre_surf = TTF_RenderUTF8_Blended(
        Fonts_getArtist(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (genre_surf) {
        SDL_BlitSurface(genre_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
        info_y += genre_surf->h + SCALE1(2);
        SDL_FreeSurface(genre_surf);
    } else {
        info_y += SCALE1(18);
    }

    // Station name - the size that the music player gives a track title, thus the
    // three playing screens hold one hierarchy.
    const char* station_name = meta->station_name[0] ? meta->station_name :
                               (current_station ? current_station->name : "Unknown Station");
    GFX_truncateText(Fonts_getTitle(), station_name, truncated, max_w_full, 0);
    SDL_Surface* name_surf = TTF_RenderUTF8_Blended(
        Fonts_getTitle(), truncated, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (name_surf) {
        SDL_BlitSurface(name_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
        info_y += name_surf->h + SCALE1(2);
        SDL_FreeSurface(name_surf);
    } else {
        info_y += SCALE1(40);
    }

    // Now Playing - Title on top (white, large), Artist below (gray, small)
    if (meta->title[0]) {
        // Title with text wrapping (max 3 lines)
        TTF_Font* title_font = Fonts_getArtist();
        const char* src = meta->title;
        int max_lines = 3;
        int lines_rendered = 0;

        while (*src && lines_rendered < max_lines) {
            // Find how many characters fit on this line
            int text_len = strlen(src);
            int char_count = text_len;

            // Binary search for characters that fit
            while (char_count > 0) {
                char line_buf[256];
                int copy_len = (char_count < 255) ? char_count : 255;
                strncpy(line_buf, src, copy_len);
                line_buf[copy_len] = '\0';

                int w, h;
                TTF_SizeUTF8(title_font, line_buf, &w, &h);
                if (w <= max_w_full) break;
                char_count--;
            }

            if (char_count == 0) char_count = 1;  // At least one character

            // Try to break at a space if not last line and not at end
            if (lines_rendered < max_lines - 1 && char_count < text_len) {
                int last_space = -1;
                for (int i = char_count - 1; i > 0; i--) {
                    if (src[i] == ' ') {
                        last_space = i;
                        break;
                    }
                }
                if (last_space > 0) char_count = last_space + 1;
            }

            // Render this line
            char line_buf[256];
            size_t copy_len = (char_count > 0 && char_count < 255) ? (size_t)char_count : 255;
            memcpy(line_buf, src, copy_len);
            line_buf[copy_len] = '\0';

            // Trim trailing space
            while (copy_len > 0 && line_buf[copy_len - 1] == ' ') {
                line_buf[--copy_len] = '\0';
            }

            if (strlen(line_buf) > 0) {
                SDL_Surface* title_surf = TTF_RenderUTF8_Blended(
                    title_font, line_buf, Theme_getColor(THEME_ROLE_PRIMARY, false));
                if (title_surf) {
                    SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
                    info_y += title_surf->h + SCALE1(2);
                    SDL_FreeSurface(title_surf);
                }
            }

            src += char_count;
            // Skip leading spaces on next line
            while (*src == ' ') src++;
            lines_rendered++;
        }
    }
    if (meta->artist[0]) {
        // Artist line (smaller font)
        GFX_truncateText(Fonts_getSmall(), meta->artist, truncated, max_w_full, 0);
        SDL_Surface* artist_surf = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (artist_surf) {
            SDL_BlitSurface(artist_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
            info_y += artist_surf->h + SCALE1(2);
            SDL_FreeSurface(artist_surf);
        }
    }

    // Show slogan if no title/artist available
    if (!meta->title[0] && !meta->artist[0] && current_station && current_station->slogan[0]) {
        GFX_truncateText(Fonts_getAlbum(), current_station->slogan, truncated, max_w_full, 0);
        SDL_Surface* slogan_surf = TTF_RenderUTF8_Blended(
            Fonts_getAlbum(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (slogan_surf) {
            SDL_BlitSurface(slogan_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
            info_y += slogan_surf->h + SCALE1(2);
            SDL_FreeSurface(slogan_surf);
        }
    }

    // The spectrum takes its box from the foot of the screen upward, above the row
    // of the state. The error message takes the same line.
    int spec_h = SCALE1(50);
    int vis_y = hh - chip_footer_height(TTF_FontHeight(Fonts_getSmall())) - spec_h;

    Spectrum_setPosition(SCALE1(PADDING), vis_y, hw - SCALE1(PADDING * 2), spec_h);

    // === THE ROW OF THE STATE ===
    // The row holds one line of text at the foot of the screen, and the buffer
    // bar at its right edge. The bar takes the x-height of the words: it has
    // their top and it sits on their baseline. The track of the bar is on the
    // surface, and its fill moves on the animation layer.
    TTF_Font* row_font = Fonts_getSmall();
    int line_h = TTF_FontHeight(row_font);
    int row_y = screen->h - SCALE1(PADDING) - line_h;
    int bar_w = SCALE1(60);
    track = (SDL_Rect){
        .x = hw - SCALE1(PADDING) - bar_w,
        .y = row_y + Fonts_getMetric(row_font, FONT_METRIC_ASCENT)
                   - Fonts_getMetric(row_font, FONT_METRIC_X_HEIGHT),
        .w = bar_w,
        .h = Fonts_getMetric(row_font, FONT_METRIC_X_HEIGHT),
    };
    status_x = SCALE1(PADDING);
    status_y = row_y;
    status_position_set = true;

    track_shown = sound_is_running();
    if (track_shown) {
        SDL_FillRect(screen, &track, Theme_getPackedColor(THEME_ROLE_PROGRESS_TRACK, false));
    }

    // Error message (displayed prominently if in error state)
    if (state == RADIO_STATE_ERROR) {
        SDL_Surface* err_text = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), Radio_getError(), Theme_getColor(THEME_ROLE_STATUS_ERROR, false));
        if (err_text) {
            SDL_BlitSurface(err_text, NULL, screen, &(SDL_Rect){SCALE1(PADDING), vis_y - SCALE1(20)});
            SDL_FreeSurface(err_text);
        }
    }
}


static void free_status_surface(void) {
    if (status_surface) {
        SDL_FreeSurface(status_surface);
        status_surface = NULL;
    }
}

// Builds the row of the state into the cached surface where a value of the
// row changed. Returns true where the surface changed, thus the caller knows
// that the status layer must paint again.
static bool build_status_surface(void) {
    if (!status_position_set) return false;

    RadioState state = Radio_getState();
    const RadioMetadata* meta = Radio_getMetadata();
    int current_bitrate = meta ? meta->bitrate : 0;

    // A station that waits for its stream connects, whatever the state of the
    // radio says: the stream of the station that went is down, and the stream of
    // this one has not started.
    bool waiting_to_start = radio_ui_waiting_to_start;

    // A station that the user stopped keeps its row, thus the screen still says
    // which station waits and at what rate. A stop with no bitrate has nothing to
    // say, thus the row goes.
    if (!waiting_to_start && state == RADIO_STATE_STOPPED && current_bitrate <= 0) {
        if (!status_holds_row) return false;
        free_status_surface();
        status_holds_row = false;
        status_last_state = state;
        return true;
    }

    // Show "buffering" only during initial connect or actual rebuffer (low buffer).
    // Once buffer is healthy, show "streaming" even if state is still BUFFERING.
    bool low_buffer = Radio_getBufferLevel() < 0.5f;

    if (status_surface && waiting_to_start == status_last_waiting &&
        state == status_last_state && current_bitrate == status_last_bitrate &&
        low_buffer == status_last_low_buffer) {
        return false;
    }

    const char* status_text = "";
    switch (state) {
        case RADIO_STATE_CONNECTING: status_text = "connecting"; break;
        case RADIO_STATE_BUFFERING:
            status_text = low_buffer ? "buffering" : "streaming";
            break;
        case RADIO_STATE_PLAYING: status_text = "streaming"; break;
        case RADIO_STATE_ERROR: status_text = "error"; break;
        case RADIO_STATE_STOPPED: status_text = "paused"; break;
        default: break;
    }
    if (waiting_to_start) status_text = "connecting";

    char bitrate_str[32] = "";
    if (current_bitrate > 0) {
        snprintf(bitrate_str, sizeof(bitrate_str), "%d kbps", current_bitrate);
    }

    // The two words of the row take one size, which is the size of the play time of
    // the music player. The color keeps them apart: the bitrate is a value, and the
    // state beside it is not.
    TTF_Font* font = Fonts_getSmall();
    int line_h = TTF_FontHeight(font);
    int gap = SCALE1(6);

    int bitrate_w = 0, bitrate_h = 0;
    if (bitrate_str[0]) TTF_SizeUTF8(font, bitrate_str, &bitrate_w, &bitrate_h);
    int status_w = 0, status_h = 0;
    if (status_text[0]) TTF_SizeUTF8(font, status_text, &status_w, &status_h);

    // The row ends before the buffer bar
    int surface_w = track.x - status_x - gap;
    if (surface_w < 1) surface_w = 1;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0,
        surface_w, line_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;
    SDL_FillRect(surface, NULL, 0);  // Transparent background

    int x_offset = 0;
    if (bitrate_str[0]) {
        SDL_Surface* text_surf = TTF_RenderUTF8_Blended(
            font, bitrate_str, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (text_surf) {
            SDL_BlitSurface(text_surf, NULL, surface, &(SDL_Rect){x_offset, (line_h - bitrate_h) / 2, 0, 0});
            SDL_FreeSurface(text_surf);
        }
        x_offset += bitrate_w + gap;
    }
    if (status_text[0]) {
        SDL_Surface* text_surf = TTF_RenderUTF8_Blended(
            font, status_text, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (text_surf) {
            SDL_BlitSurface(text_surf, NULL, surface, &(SDL_Rect){x_offset, (line_h - status_h) / 2, 0, 0});
            SDL_FreeSurface(text_surf);
        }
    }

    free_status_surface();
    status_surface = surface;
    status_holds_row = true;
    status_last_state = state;
    status_last_bitrate = current_bitrate;
    status_last_waiting = waiting_to_start;
    status_last_low_buffer = low_buffer;
    return true;
}

// The status layer: the row of the state
static void paint_status_layer(void) {
    UiLayer_clear(UI_LAYER_STATUS);
    if (status_surface) UiLayer_blit(status_surface, status_x, status_y, UI_LAYER_STATUS);
}

// The width of the fill of the buffer bar, and 0 where the bar does not draw
static int fill_width(void) {
    if (!status_position_set || !sound_is_running()) return 0;
    int w = (int)(track.w * Radio_getBufferLevel());
    if (w > track.w) w = track.w;
    return w < 0 ? 0 : w;
}

// The animation layer: the spectrum, and the fill of the buffer bar
static void paint_animation_layer(void) {
    UiLayer_clear(UI_LAYER_ANIMATION);
    if (Spectrum_isShowing()) Spectrum_paint(UI_LAYER_ANIMATION);

    int w = fill_width();
    fill_shown_w = w;
    if (w <= 0) return;
    SDL_Surface* fill = SDL_CreateRGBSurfaceWithFormat(0, w, track.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!fill) return;
    SDL_FillRect(fill, NULL, Theme_getPackedColor(THEME_ROLE_PROGRESS_FILL, false));
    UiLayer_blit(fill, track.x, track.y, UI_LAYER_ANIMATION);
    SDL_FreeSurface(fill);
}

void RadioPlaying_paintLayers(void) {
    // The screen keeps the selection layer clear
    UiLayer_clear(UI_LAYER_SELECTION);
    build_status_surface();
    paint_status_layer();
    paint_animation_layer();
}

bool RadioPlaying_frame(bool surface_renders) {
    // The track of the bar comes and goes with the state, on the surface
    bool needs_surface = status_position_set && sound_is_running() != track_shown;

    // A change of the row reaches the layer in this frame
    bool repaint_status = build_status_surface();

    // The bars move on each frame while they draw, and the frame after the
    // last one takes them away. A spectrum that draws nothing needs neither.
    bool repaint_animation = false;
    if (Spectrum_needsRefresh()) {
        Spectrum_update();
        static bool was_showing = false;
        bool showing = Spectrum_isShowing();
        if (showing || was_showing) repaint_animation = true;
        was_showing = showing;
    }
    // The fill follows the buffer, without the surface
    if (fill_width() != fill_shown_w) repaint_animation = true;

    if (layers_stale) {
        repaint_status = repaint_animation = true;
        layers_stale = false;
    }

    if (surface_renders) return needs_surface;
    if (repaint_status)    paint_status_layer();
    if (repaint_animation) paint_animation_layer();
    return needs_surface;
}

void RadioPlaying_invalidate(void) {
    layers_stale = true;
}

void RadioPlaying_leave(void) {
    UiLayer_clear(UI_LAYER_STATUS);
    UiLayer_clear(UI_LAYER_ANIMATION);
    status_position_set = false;
    track_shown = false;
    fill_shown_w = -1;
    free_status_surface();
    status_holds_row = false;
    status_last_state = RADIO_STATE_STOPPED;
    status_last_bitrate = 0;
    status_last_waiting = false;
}
