#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_music_playing.h"
#include "ui_fonts.h"
#include "ui_layers.h"
#include "ui_utils.h"
#include "ui_theme.h"
#include "ui_album_art.h"
#include "spectrum.h"
#include "lyrics.h"
#include "lyric_window.h"
#include "settings.h"

// The marquee of the title
static ScrollTextState title_scroll;

// The play time on the status layer. The cache holds what the layer shows,
// thus a frame paints the time only where the value moved. A cache of -1 says
// that the layer holds no time, whatever the position is.
static int  time_x = 0, time_y = 0;
static bool time_position_set = false;
static int  time_shown_second = -1;
static int  time_shown_duration = -1;

// The lyric window on the surface. What the last render drew, thus a frame
// asks for a render only where the window changes.
static int lyrics_shown_current = -1;
static int lyrics_shown_count = 0;

// Set where the display lost the layers, thus the next frame paints each one
static bool layers_stale = false;

// The gap between two lyric rows
#define LYRIC_ROW_GAP 2

// Draw one playback mode indicator with its right edge at `right_x`. The label
// takes the primary text role and a rule below it where the mode is on, and the
// secondary text role with no rule where it is off. Gives the width that it drew,
// thus a caller places the next one beside it.
static int draw_mode_indicator(SDL_Surface *screen, const char *label, bool on,
                               int right_x, int y) {
    SDL_Color color = Theme_getColor(on ? THEME_ROLE_PRIMARY : THEME_ROLE_SECONDARY, false);
    SDL_Surface *surf = TTF_RenderUTF8_Blended(Fonts_getTiny(), label, color);
    if (!surf) return 0;

    int x = right_x - surf->w;
    int w = surf->w;
    SDL_BlitSurface(surf, NULL, screen, &(SDL_Rect){x, y});
    // The rule takes the color of its label, thus the two cannot drift
    if (on) {
        SDL_Rect rule = {x, y + surf->h, w, SCALE1(1)};
        SDL_FillRect(screen, &rule, SDL_MapRGB(screen->format, color.r, color.g, color.b));
    }
    SDL_FreeSurface(surf);
    return w;
}

int MusicPlaying_lyricRows(int height, int row_h, int gap) {
    if (row_h <= 0 || height < row_h) return 0;
    return (height + gap) / (row_h + gap);
}

void MusicPlaying_drawLyricRow(SDL_Surface* screen, TTF_Font* font, const char* text,
                               SDL_Color color, int x, int y, int max_w) {
    if (!text || !text[0] || max_w <= 0) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    SDL_Rect src = {0, 0, surf->w > max_w ? max_w : surf->w, surf->h};
    SDL_BlitSurface(surf, &src, screen, &(SDL_Rect){x, y, 0, 0});
    SDL_FreeSurface(surf);
}

// Draws the lyric window in the room between `top` and `bottom`, and records
// what it drew. The current lyric takes the primary text role, and each other
// lyric the secondary one.
static void draw_lyric_window(SDL_Surface* screen, int x, int top, int bottom, int max_w) {
    TTF_Font* font = Fonts_getSmall();
    int row_h = TTF_FontHeight(font);
    int gap = SCALE1(LYRIC_ROW_GAP);
    int rows = MusicPlaying_lyricRows(bottom - top, row_h, gap);

    int count = Lyrics_lineCount();
    int current = Lyrics_currentIndex(Player_getPosition());
    LyricWindow window = LyricWindow_layout(count, current, rows);

    for (int row = 0; row < window.count; row++) {
        ThemeRole role = (row == window.current_row) ? THEME_ROLE_PRIMARY : THEME_ROLE_SECONDARY;
        MusicPlaying_drawLyricRow(screen, font, Lyrics_lineText(window.first + row),
                                  Theme_getColor(role, false), x, top + row * (row_h + gap), max_w);
    }

    lyrics_shown_current = current;
    lyrics_shown_count = count;
}

void render_playing(SDL_Surface *screen, int show_setting, BrowserContext *browser,
                    bool shuffle_enabled, bool repeat_enabled,
                    int playlist_track_num, int playlist_total) {
    GFX_clear(screen);

    // Render album art as triangular background (if available)
    SDL_Surface *album_art = Player_getAlbumArt();
    if (album_art && album_art->w > 0 && album_art->h > 0) {
        render_album_art_background(screen, album_art);
    }

    int hw = screen->w;
    int hh = screen->h;

    const TrackInfo *info = Player_getTrackInfo();
    AudioFormat format = Player_detectFormat(Player_getCurrentFile());

    // === TOP BAR ===
    int top_y = top_of_the_chip_box(screen, chip_height());

    // Format chip
    const char *fmt_name = get_format_name(format);
    SDL_Rect chip = draw_chip(screen, fmt_name, SCALE1(PADDING), top_y);

    // Track counter "01 - 03" (smaller, gray) - after the format badge
    // Use playlist counts if available (playlist_total > 0), otherwise use browser counts
    int track_num = (playlist_total > 0) ? playlist_track_num : Browser_getCurrentTrackNumber(browser);
    int total_tracks = (playlist_total > 0) ? playlist_total : Browser_countAudioFiles(browser);
    char track_str[32];
    snprintf(track_str, sizeof(track_str), "%02d - %02d", track_num, total_tracks);
    SDL_Surface *track_surf = TTF_RenderUTF8_Blended(
        Fonts_getTiny(), track_str, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (track_surf) {
        int track_x = chip.x + chip.w + SCALE1(8);
        int track_y = top_y + (chip.h - track_surf->h) / 2;
        SDL_BlitSurface(track_surf, NULL, screen, &(SDL_Rect){track_x, track_y});
        SDL_FreeSurface(track_surf);
    }

    // Hardware status (clock, battery) on right
    if (screen_has_status_group(screen)) GFX_blitHardwareGroup(screen, show_setting);

    // === TRACK INFO SECTION ===
    int info_y = total_header_height(screen, chip_height());
    char truncated[256];

    // Each line of the block takes the same width, thus a line that fits the screen
    // never scrolls and a line that does not fit comes in from the edge.
    int max_w_text = hw - SCALE1(PADDING * 1);

    // Artist name (Medium font, gray)
    const char *artist = info->artist[0] ? info->artist : "Unknown Artist";
    GFX_truncateText(Fonts_getArtist(), artist, truncated, max_w_text, 0);
    SDL_Surface *artist_surf = TTF_RenderUTF8_Blended(
        Fonts_getArtist(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (artist_surf) {
        SDL_BlitSurface(artist_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
        info_y += artist_surf->h + SCALE1(2);
        SDL_FreeSurface(artist_surf);
    } else {
        info_y += SCALE1(18);
    }

    // Song title (Regular font extra large, white) - with GPU scrolling animation (no background)
    const char *title = info->title[0] ? info->title : "Unknown Title";
    int title_y = info_y; // Save for GPU scroll

    // Check if text changed and reset scroll state.
    if (strcmp(title_scroll.text, title) != 0) {
        ScrollText_reset(&title_scroll, title, Fonts_getTitle(), max_w_text,
                         THEME_ROLE_PRIMARY, false, true);
    }
    SDL_Color title_color = Theme_getColor(THEME_ROLE_PRIMARY, false);

    // Activate scroll after delay (this render path bypasses ScrollText_render)
    ScrollText_activateAfterDelay(&title_scroll);

    // A title that moves goes on the animation layer, thus the surface holds no
    // title under it. A title at rest goes on the surface, cut at the width of
    // the block.
    if (title_scroll.needs_scroll) {
        title_scroll.last_x = SCALE1(PADDING);
        title_scroll.last_y = title_y;
        title_scroll.last_font = Fonts_getTitle();
        title_scroll.last_color = title_color;
    } else {
        SDL_Surface *title_surf = TTF_RenderUTF8_Blended(
            Fonts_getTitle(), title, title_color);
        if (title_surf) {
            SDL_Rect src = {0, 0, title_surf->w > max_w_text ? max_w_text : title_surf->w,
                            title_surf->h};
            SDL_BlitSurface(title_surf, &src, screen, &(SDL_Rect){SCALE1(PADDING), title_y, 0, 0});
            SDL_FreeSurface(title_surf);
        }
    }
    info_y += TTF_FontHeight(Fonts_getTitle()) + SCALE1(2);

    // === SPECTRUM SECTION (GPU rendered) ===
    // The row of the play time and the indicators is the foot of this screen, thus the
    // spectrum stops above it.
    int indicator_h = TTF_FontHeight(Fonts_getTiny()) + SCALE1(1);
    int spec_h = SCALE1(50);
    int spec_y = hh - chip_footer_height(indicator_h) - spec_h;
    int spec_x = SCALE1(PADDING);
    int spec_w = hw - SCALE1(PADDING * 2);

    // Set position for GPU rendering (actual rendering happens in main loop)
    Spectrum_setPosition(spec_x, spec_y, spec_w, spec_h);

    // The lyric window takes the room between the title and the spectrum. The
    // album name takes its first row where the lyrics are off.
    if (Settings_getLyricsEnabled()) {
        draw_lyric_window(screen, SCALE1(PADDING), info_y, spec_y - SCALE1(PADDING), max_w_text);
    } else {
        lyrics_shown_current = -1;
        lyrics_shown_count = 0;
        const char *album = info->album[0] ? info->album : "";
        if (album[0]) {
            GFX_truncateText(Fonts_getSmall(), album, truncated, max_w_text, 0);
            SDL_Surface *album_surf = TTF_RenderUTF8_Blended(
                Fonts_getSmall(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
            if (album_surf) {
                SDL_BlitSurface(album_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
                SDL_FreeSurface(album_surf);
            }
        }
    }

    // === BOTTOM BAR ===
    // The screen draws no button hint, thus the row of the play time and the
    // indicators takes the bottom margin of the screen.
    int bottom_y = top_of_the_footer_chip_box(screen, indicator_h);

    // The time is on the status layer, thus the render records its place only
    time_x = SCALE1(PADDING);
    time_y = top_of_the_footer_chip_box(screen, TTF_FontHeight(Fonts_getSmall()));
    time_position_set = true;

    // The indicators fill from the right margin toward the middle.
    int label_x = hw - SCALE1(PADDING);
    label_x -= draw_mode_indicator(screen, "REPEAT", repeat_enabled, label_x, bottom_y);
    label_x -= SCALE1(12);
    label_x -= draw_mode_indicator(screen, "SHUFFLE", shuffle_enabled, label_x, bottom_y);
    label_x -= SCALE1(12);
    draw_mode_indicator(screen, "LYRICS", Settings_getLyricsEnabled(), label_x, bottom_y);
}

// True while the marquee of the title has something to draw on the layer
static bool title_marquee_showing(void) {
    return title_scroll.text[0] && title_scroll.needs_scroll && title_scroll.last_font != NULL;
}

// Paints the marquee of the title and moves it. The title of a sound that does
// not play stands still, thus the paint keeps its place on that frame.
static void paint_title_marquee(void) {
    int offset = title_scroll.scroll_offset;
    ScrollText_paintGPU(&title_scroll, title_scroll.last_font, title_scroll.last_color,
                        title_scroll.last_x, title_scroll.last_y);
    if (Player_getState() != PLAYER_STATE_PLAYING) {
        title_scroll.scroll_offset = offset;
    }
}

// The animation layer: the spectrum, and the marquee of the title over it.
static void paint_animation_layer(void) {
    UiLayer_clear(UI_LAYER_ANIMATION);
    if (Spectrum_isShowing())    Spectrum_paint(UI_LAYER_ANIMATION);
    if (title_marquee_showing()) paint_title_marquee();
}

// The status layer: the play time
static void paint_status_layer(void) {
    UiLayer_clear(UI_LAYER_STATUS);
    if (!time_position_set) return;

    int position = Player_getPosition();
    int duration = Player_getDuration();
    time_shown_second = position / 1000;
    time_shown_duration = duration;

    // The position and the total take one font, thus the two sit on one baseline
    // and the pair is one box. The separator gives a relation and is not a value,
    // thus it goes with the total.
    char pos_str[16];
    format_time(pos_str, position);
    SDL_Surface *pos_surf = TTF_RenderUTF8_Blended(
        Fonts_getSmall(), pos_str, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (!pos_surf) return;

    char total_str[24];
    char dur_str[16];
    format_time(dur_str, duration);
    snprintf(total_str, sizeof(total_str), "/%s", dur_str);
    SDL_Surface *dur_surf = TTF_RenderUTF8_Blended(
        Fonts_getSmall(), total_str, Theme_getColor(THEME_ROLE_SECONDARY, false));

    int total_w = pos_surf->w + (dur_surf ? dur_surf->w : 0);
    int total_h = pos_surf->h;

    SDL_Surface *combined = SDL_CreateRGBSurfaceWithFormat(0, total_w, total_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (combined) {
        SDL_FillRect(combined, NULL, 0); // Transparent background
        SDL_BlitSurface(pos_surf, NULL, combined, &(SDL_Rect){0, 0, 0, 0});
        if (dur_surf) {
            SDL_BlitSurface(dur_surf, NULL, combined, &(SDL_Rect){pos_surf->w, 0, 0, 0});
        }
        UiLayer_blit(combined, time_x, time_y, UI_LAYER_STATUS);
        SDL_FreeSurface(combined);
    }

    SDL_FreeSurface(pos_surf);
    if (dur_surf) SDL_FreeSurface(dur_surf);
}

void MusicPlaying_paintLayers(void) {
    // The screen keeps the selection layer clear
    UiLayer_clear(UI_LAYER_SELECTION);
    paint_status_layer();
    paint_animation_layer();
}

bool MusicPlaying_frame(bool surface_renders) {
    bool needs_surface = false;
    bool playing = Player_getState() == PLAYER_STATE_PLAYING;

    // The marquee of the title starts after its delay. The title at rest leaves
    // the surface on that frame, thus the surface renders.
    if (ScrollText_needsRender(&title_scroll)) {
        ScrollText_activateAfterDelay(&title_scroll);
        if (title_scroll.needs_scroll) needs_surface = true;
    }

    // The window follows the current lyric, and it fills when the lyrics arrive
    if (Settings_getLyricsEnabled()) {
        if (Lyrics_currentIndex(Player_getPosition()) != lyrics_shown_current ||
            Lyrics_lineCount() != lyrics_shown_count) {
            needs_surface = true;
        }
    }

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
    if (playing && ScrollText_isScrolling(&title_scroll)) repaint_animation = true;

    // The time moves once a second while the sound plays. A layer that holds no
    // time, after a clear, takes it whatever the position is.
    bool repaint_status = time_position_set &&
                          (time_shown_second < 0 ||
                           (playing && (Player_getPosition() / 1000 != time_shown_second ||
                                        Player_getDuration() != time_shown_duration)));

    if (layers_stale) {
        repaint_status = repaint_animation = true;
        layers_stale = false;
    }

    if (surface_renders) return needs_surface;
    if (repaint_status)    paint_status_layer();
    if (repaint_animation) paint_animation_layer();
    return needs_surface;
}

void MusicPlaying_invalidate(void) {
    layers_stale = true;
}

void MusicPlaying_leave(void) {
    UiLayer_clear(UI_LAYER_STATUS);
    UiLayer_clear(UI_LAYER_ANIMATION);
    time_position_set = false;
    time_shown_second = -1;
    time_shown_duration = -1;
    lyrics_shown_current = -1;
    lyrics_shown_count = 0;
}
