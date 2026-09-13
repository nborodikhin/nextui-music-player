#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "ui_podcast_playing.h"
#include "ui_podcast.h"
#include "podcast.h"
#include "player.h"
#include "spectrum.h"
#include "ui_fonts.h"
#include "ui_layers.h"
#include "ui_utils.h"
#include "ui_theme.h"
#include "ui_album_art.h"
#include "wget_fetch.h"

// The marquee of the episode title
static ScrollTextState title_scroll = {0};

// Podcast artwork state
static SDL_Surface* podcast_artwork = NULL;
static char podcast_artwork_url[512] = {0};

// The row of the play time at the foot of the screen. The render records
// where the time and the track of the bar go, and the painter of the status
// layer draws the time and the fill there. The cache holds the second that
// the layer shows, and -1 where the layer holds no row.
static int      time_x = 0, time_y = 0;
static SDL_Rect track = {0};
static int      progress_duration_ms = 0;
static bool     progress_position_set = false;
static int      progress_shown_sec = -1;

// Set where the display lost the layers, thus the next frame paints each one
static bool layers_stale = false;

// Fetch podcast artwork from URL (cached in podcast folder)
// feed_id: the podcast's feed_id for storing artwork in its folder
static void podcast_fetch_artwork(const char* artwork_url, const char* feed_id) {
    if (!artwork_url || !artwork_url[0] || !feed_id || !feed_id[0]) return;

    // Already have this artwork
    if (strcmp(podcast_artwork_url, artwork_url) == 0 && podcast_artwork) return;

    // Clear old artwork and invalidate album art background cache
    if (podcast_artwork) {
        SDL_FreeSurface(podcast_artwork);
        podcast_artwork = NULL;
        cleanup_album_art_background();
    }
    strncpy(podcast_artwork_url, artwork_url, sizeof(podcast_artwork_url) - 1);

    // Build cache path: <podcast_data_dir>/<feed_id>/artwork.jpg
    char feed_dir[512];
    Podcast_getFeedDataPath(feed_id, feed_dir, sizeof(feed_dir));

    char cache_path[768];
    snprintf(cache_path, sizeof(cache_path), "%s/artwork.jpg", feed_dir);

    // Try to load from cache first
    FILE* f = fopen(cache_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size > 0 && size < PODCAST_ARTWORK_MAX_SIZE) {
            uint8_t* data = (uint8_t*)malloc(size);
            if (data && fread(data, 1, size, f) == (size_t)size) {
                if (Podcast_imageIsComplete(data, size)) {
                    SDL_RWops* rw = SDL_RWFromConstMem(data, size);
                    if (rw) {
                        SDL_Surface* loaded = IMG_Load_RW(rw, 1);
                        podcast_artwork = Podcast_surfaceToArgb8888(loaded);
                    }
                }
            }
            free(data);
        }
        fclose(f);
        if (podcast_artwork) return;
        // Cached file is corrupt/incomplete — delete it so we re-fetch
        remove(cache_path);
    }

    // Fetch from network using static buffer
    static uint8_t artwork_buffer[PODCAST_ARTWORK_MAX_SIZE];
    int size = wget_fetch_bytes(artwork_url, artwork_buffer, PODCAST_ARTWORK_MAX_SIZE);

    if (size > 0 && Podcast_imageIsComplete(artwork_buffer, size)) {
        // Save to podcast folder (directory should already exist from subscription)
        f = fopen(cache_path, "wb");
        if (f) {
            fwrite(artwork_buffer, 1, size, f);
            fclose(f);
        }

        // Load as SDL surface and convert to ARGB8888 for proper scaling
        SDL_RWops* rw = SDL_RWFromConstMem(artwork_buffer, size);
        if (rw) {
            SDL_Surface* loaded = IMG_Load_RW(rw, 1);
            podcast_artwork = Podcast_surfaceToArgb8888(loaded);
        }
    }
}

void render_podcast_playing(SDL_Surface* screen, int show_setting,
                             int feed_index, int episode_index) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    PodcastFeed* feed = Podcast_getSubscription(feed_index);
    PodcastEpisode* ep = Podcast_getEpisode(feed_index, episode_index);

    if (!feed || !ep) {
        render_screen_header(screen, "Now Playing", show_setting);
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // Fetch and render album art background (if available)
    if (feed->artwork_url[0] && feed->feed_id[0]) {
        podcast_fetch_artwork(feed->artwork_url, feed->feed_id);
        if (podcast_artwork && podcast_artwork->w > 0 && podcast_artwork->h > 0) {
            render_album_art_background(screen, podcast_artwork);
        }
    }

    // === TOP BAR ===
    int top_y = top_of_the_chip_box(screen, chip_height());

    // Source chip
    const char* chip_text = "PODCAST";
    SDL_Rect chip = draw_chip(screen, chip_text, SCALE1(PADDING), top_y);

    int next_chip_x = chip.x + chip.w;

    // Playback speed badge (show when not 1x, right after PODCAST badge)
    float pspeed = Player_getPlaybackSpeed();
    if (pspeed != 1.0f) {
        char speed_label[16];
        snprintf(speed_label, sizeof(speed_label), "%.2gx", pspeed);
        SDL_Surface* speed_surf = TTF_RenderUTF8_Blended(
            Fonts_getTiny(), speed_label, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (speed_surf) {
            int sx = next_chip_x + SCALE1(4);
            int speed_chip_w = speed_surf->w + SCALE1(10);
            int speed_chip_h = speed_surf->h + SCALE1(4);
            uint32_t outline = Theme_getPackedColor(THEME_ROLE_SECONDARY, false);
            SDL_FillRect(screen, &(SDL_Rect){sx, top_y, speed_chip_w, 1}, outline);
            SDL_FillRect(screen,
                         &(SDL_Rect){sx, top_y + speed_chip_h - 1, speed_chip_w, 1}, outline);
            SDL_FillRect(screen, &(SDL_Rect){sx, top_y, 1, speed_chip_h}, outline);
            SDL_FillRect(screen,
                         &(SDL_Rect){sx + speed_chip_w - 1, top_y, 1, speed_chip_h}, outline);
            SDL_BlitSurface(speed_surf, NULL, screen, &(SDL_Rect){sx + SCALE1(5), top_y + SCALE1(2)});
            next_chip_x = sx + speed_chip_w;
            SDL_FreeSurface(speed_surf);
        }
    }

    // Episode counter "01 / 67" (like track counter in music player)
    // Show position among downloaded episodes, not total episodes
    int downloaded_total = Podcast_countDownloadedEpisodes(feed_index);
    int downloaded_idx = Podcast_getDownloadedEpisodeIndex(feed_index, episode_index);
    char ep_counter[32];
    if (downloaded_idx >= 0 && downloaded_total > 0) {
        snprintf(ep_counter, sizeof(ep_counter), "%02d / %02d", downloaded_idx + 1, downloaded_total);
    } else {
        // Fallback if episode is not downloaded (shouldn't happen in playing state)
        snprintf(ep_counter, sizeof(ep_counter), "%02d / %02d", episode_index + 1, feed->episode_count);
    }
    SDL_Surface* counter_surf = TTF_RenderUTF8_Blended(
        Fonts_getTiny(), ep_counter, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (counter_surf) {
        int counter_x = next_chip_x + SCALE1(8);
        int counter_y = top_y + (chip.h - counter_surf->h) / 2;
        SDL_BlitSurface(counter_surf, NULL, screen, &(SDL_Rect){counter_x, counter_y});
        SDL_FreeSurface(counter_surf);
    }

    // Hardware status (clock, battery) on right
    if (screen_has_status_group(screen)) GFX_blitHardwareGroup(screen, show_setting);

    // === PODCAST INFO SECTION (like music player artist/title/album) ===
    int info_y = total_header_height(screen, chip_height());
    int max_w_text = hw - SCALE1(PADDING * 2);

    // Podcast name (like Artist in music player) - gray, artist font
    GFX_truncateText(Fonts_getArtist(), feed->title, truncated, max_w_text, 0);
    SDL_Surface* podcast_surf = TTF_RenderUTF8_Blended(
        Fonts_getArtist(), truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (podcast_surf) {
        SDL_BlitSurface(podcast_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
        info_y += podcast_surf->h + SCALE1(2);
        SDL_FreeSurface(podcast_surf);
    } else {
        info_y += SCALE1(18);
    }

    // Episode title (like Title in music player) - white, title font, with scrolling
    const char* title = ep->title[0] ? ep->title : "Unknown Episode";
    int title_y = info_y;

    // Check if text changed and reset scroll state
    if (strcmp(title_scroll.text, title) != 0 || title_scroll.role != THEME_ROLE_PRIMARY) {
        ScrollText_reset(&title_scroll, title, Fonts_getTitle(), max_w_text,
                         THEME_ROLE_PRIMARY, false, true);
    }

    // Activate scroll after delay (this render path bypasses ScrollText_render)
    ScrollText_activateAfterDelay(&title_scroll);

    // A title that moves goes on the animation layer, and the painter of that
    // layer draws it. A title that fits goes on the surface of this screen.
    if (title_scroll.needs_scroll) {
        title_scroll.last_x     = SCALE1(PADDING);
        title_scroll.last_y     = title_y;
        title_scroll.last_font  = Fonts_getTitle();
        title_scroll.last_color = Theme_getColor(THEME_ROLE_PRIMARY, false);
    } else {
        SDL_Surface* title_surf = TTF_RenderUTF8_Blended(
            Fonts_getTitle(), title, Theme_getColor(THEME_ROLE_PRIMARY, false));
        if (title_surf) {
            SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), title_y, 0, 0});
            SDL_FreeSurface(title_surf);
        }
    }
    info_y += TTF_FontHeight(Fonts_getTitle()) + SCALE1(2);

    // The row of the play time and the bar of progress is the foot of this screen,
    // as it is on the music player. The spectrum takes its box above that row, and
    // the description takes the room that is left.
    int row_h = TTF_FontHeight(Fonts_getSmall());
    int spec_h = SCALE1(50);
    int spec_y = hh - chip_footer_height(row_h) - spec_h;
    int spec_x = SCALE1(PADDING);
    int spec_w = hw - SCALE1(PADDING * 2);

    Spectrum_setPosition(spec_x, spec_y, spec_w, spec_h);

    // Episode description. The count of lines comes from the room between the
    // title and the box of the spectrum, thus a screen with more room shows more.
    if (ep->description[0]) {
        TTF_Font* desc_font = Fonts_getSmall();
        int desc_line_h = TTF_FontHeight(desc_font);

        int max_lines = (spec_y - info_y) / desc_line_h;
        if (max_lines < 0) max_lines = 0;

        // Strip HTML tags and newlines from description
        char desc_buf[512];
        int di = 0;
        bool in_tag = false;
        for (const char* sp = ep->description; *sp && di < 511; sp++) {
            if (*sp == '<') { in_tag = true; continue; }
            if (*sp == '>') { in_tag = false; continue; }
            if (in_tag) continue;
            if (*sp == '\n' || *sp == '\r') { desc_buf[di++] = ' '; continue; }
            if (*sp == '&') {
                if (strncmp(sp, "&amp;", 5) == 0) { desc_buf[di++] = '&'; sp += 4; }
                else if (strncmp(sp, "&lt;", 4) == 0) { desc_buf[di++] = '<'; sp += 3; }
                else if (strncmp(sp, "&gt;", 4) == 0) { desc_buf[di++] = '>'; sp += 3; }
                else if (strncmp(sp, "&quot;", 6) == 0) { desc_buf[di++] = '"'; sp += 5; }
                else if (strncmp(sp, "&apos;", 6) == 0) { desc_buf[di++] = '\''; sp += 5; }
                else if (strncmp(sp, "&#39;", 5) == 0) { desc_buf[di++] = '\''; sp += 4; }
                else if (strncmp(sp, "&nbsp;", 6) == 0) { desc_buf[di++] = ' '; sp += 5; }
                else desc_buf[di++] = '&';
                continue;
            }
            desc_buf[di++] = *sp;
        }
        desc_buf[di] = '\0';

        const char* remaining = desc_buf;
        for (int line = 0; line < max_lines && *remaining; line++) {
            int tw;
            TTF_SizeUTF8(desc_font, remaining, &tw, NULL);

            if (tw <= max_w_text || line == max_lines - 1) {
                GFX_truncateText(desc_font, remaining, truncated, max_w_text, 0);
                SDL_Surface* d = TTF_RenderUTF8_Blended(
                    desc_font, truncated, Theme_getColor(THEME_ROLE_SECONDARY, false));
                if (d) {
                    SDL_BlitSurface(d, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
                    info_y += d->h;
                    SDL_FreeSurface(d);
                }
                break;
            }

            const char* p = remaining;
            const char* last_break = remaining;
            while (*p) {
                while (*p && *p != ' ') p++;
                int seg_len = p - remaining;
                char measure[512];
                if (seg_len >= 512) seg_len = 511;
                memcpy(measure, remaining, seg_len);
                measure[seg_len] = '\0';
                TTF_SizeUTF8(desc_font, measure, &tw, NULL);
                if (tw > max_w_text) break;
                last_break = p;
                while (*p == ' ') p++;
            }

            if (last_break == remaining) break;

            int line_len = last_break - remaining;
            char line_buf[512];
            if (line_len >= 512) line_len = 511;
            memcpy(line_buf, remaining, line_len);
            line_buf[line_len] = '\0';

            SDL_Surface* d = TTF_RenderUTF8_Blended(
                desc_font, line_buf, Theme_getColor(THEME_ROLE_SECONDARY, false));
            if (d) {
                SDL_BlitSurface(d, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
                info_y += d->h;
                SDL_FreeSurface(d);
            }

            remaining = last_break;
            while (*remaining == ' ') remaining++;
        }
    }

    // === THE ROW OF THE PLAY TIME ===
    // The time and the bar of progress share the bottom row. The time is at the
    // left margin and the bar takes the room that is left, to the right margin.
    // The track of the bar is on the surface, thus its place must not move with
    // the digits of the time: the column of the time takes the width of the
    // duration with each digit at its widest, and the bar starts after it.
    int row_y = top_of_the_footer_chip_box(screen, row_h);
    int duration_ms = Podcast_getDuration();
    TTF_Font* time_font = Fonts_getSmall();
    char dur_str[16], total_str[24], template_str[16];
    Podcast_formatDuration(dur_str, duration_ms / 1000);
    snprintf(total_str, sizeof(total_str), "/%s", dur_str);
    for (int i = 0; i <= (int)strlen(dur_str); i++) {
        template_str[i] = (dur_str[i] >= '0' && dur_str[i] <= '9') ? '0' : dur_str[i];
    }
    int cur_w = 0, total_w = 0;
    TTF_SizeUTF8(time_font, template_str, &cur_w, NULL);
    TTF_SizeUTF8(time_font, total_str, &total_w, NULL);

    // The play time is figures, thus the bar centers on the digit height and
    // not on the x-height. It keeps the thickness of the x-height, as each
    // mark of the app does.
    int bar_h = Fonts_getMetric(time_font, FONT_METRIC_X_HEIGHT);
    int digit_h = Fonts_getMetric(time_font, FONT_METRIC_DIGIT_HEIGHT);
    int ascent = Fonts_getMetric(time_font, FONT_METRIC_ASCENT);
    int bar_x = SCALE1(PADDING) + cur_w + total_w + SCALE1(12);
    track = (SDL_Rect){
        .x = bar_x,
        .y = row_y + ascent - digit_h + (digit_h - bar_h) / 2,
        .w = hw - SCALE1(PADDING) - bar_x,
        .h = bar_h,
    };
    time_x = SCALE1(PADDING);
    time_y = row_y;
    progress_duration_ms = duration_ms;
    progress_position_set = true;

    if (track.w > 0) {
        SDL_FillRect(screen, &track, Theme_getPackedColor(THEME_ROLE_PROGRESS_TRACK, false));
    }
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

// The animation layer: the spectrum, and the marquee of the title over it
static void paint_animation_layer(void) {
    UiLayer_clear(UI_LAYER_ANIMATION);
    if (Spectrum_isShowing())    Spectrum_paint(UI_LAYER_ANIMATION);
    if (title_marquee_showing()) paint_title_marquee();
}

// The status layer: the play time, and the fill of the progress bar
static void paint_status_layer(void) {
    UiLayer_clear(UI_LAYER_STATUS);
    if (!progress_position_set) return;

    int position_ms = Player_getPosition();
    int position_sec = position_ms / 1000;
    progress_shown_sec = position_sec;

    int duration_ms = progress_duration_ms > 0 ? progress_duration_ms : Podcast_getDuration();

    // The position and the total take one font on one line, as the play time of the
    // music player does. The separator gives a relation and is not a value, thus it
    // goes with the total.
    char time_cur[16], time_dur[16], time_total[24];
    Podcast_formatDuration(time_cur, position_sec);
    Podcast_formatDuration(time_dur, duration_ms / 1000);
    snprintf(time_total, sizeof(time_total), "/%s", time_dur);

    TTF_Font* time_font = Fonts_getSmall();
    int cur_w = 0;
    TTF_SizeUTF8(time_font, time_cur, &cur_w, NULL);

    SDL_Surface* cur_surf = TTF_RenderUTF8_Blended(
        time_font, time_cur, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (cur_surf) {
        UiLayer_blit(cur_surf, time_x, time_y, UI_LAYER_STATUS);
        SDL_FreeSurface(cur_surf);
    }
    SDL_Surface* dur_surf = TTF_RenderUTF8_Blended(
        time_font, time_total, Theme_getColor(THEME_ROLE_SECONDARY, false));
    if (dur_surf) {
        UiLayer_blit(dur_surf, time_x + cur_w, time_y, UI_LAYER_STATUS);
        SDL_FreeSurface(dur_surf);
    }

    if (track.w > 0 && duration_ms > 0) {
        int fill_w = (int)((int64_t)track.w * position_ms / duration_ms);
        if (fill_w > track.w) fill_w = track.w;
        if (fill_w > 0) {
            SDL_Surface* fill = SDL_CreateRGBSurfaceWithFormat(0, fill_w, track.h, 32,
                                                               SDL_PIXELFORMAT_ARGB8888);
            if (fill) {
                SDL_FillRect(fill, NULL, Theme_getPackedColor(THEME_ROLE_PROGRESS_FILL, false));
                UiLayer_blit(fill, track.x, track.y, UI_LAYER_STATUS);
                SDL_FreeSurface(fill);
            }
        }
    }
}

void PodcastPlaying_paintLayers(void) {
    // The screen keeps the selection layer clear
    UiLayer_clear(UI_LAYER_SELECTION);
    paint_status_layer();
    paint_animation_layer();
}

bool PodcastPlaying_frame(bool surface_renders) {
    bool needs_surface = false;
    bool playing = Player_getState() == PLAYER_STATE_PLAYING;

    // The marquee of the title starts after its delay. The title at rest leaves
    // the surface on that frame, thus the surface renders.
    if (ScrollText_needsRender(&title_scroll)) {
        ScrollText_activateAfterDelay(&title_scroll);
        if (title_scroll.needs_scroll) needs_surface = true;
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

    // The row moves once a second while the sound plays. A layer that holds no
    // row, after a clear, takes it whatever the position is.
    bool repaint_status = progress_position_set &&
                          (progress_shown_sec < 0 ||
                           (playing && Player_getPosition() / 1000 != progress_shown_sec));

    if (layers_stale) {
        repaint_status = repaint_animation = true;
        layers_stale = false;
    }

    if (surface_renders) return needs_surface;
    if (repaint_status)    paint_status_layer();
    if (repaint_animation) paint_animation_layer();
    return needs_surface;
}

void PodcastPlaying_invalidate(void) {
    layers_stale = true;
}

void PodcastPlaying_leave(void) {
    if (podcast_artwork) {
        // The background keeps a cache by the address of its source
        cleanup_album_art_background();
        SDL_FreeSurface(podcast_artwork);
        podcast_artwork = NULL;
    }
    podcast_artwork_url[0] = '\0';
    ScrollText_forget(&title_scroll);
    UiLayer_clear(UI_LAYER_STATUS);
    UiLayer_clear(UI_LAYER_ANIMATION);
    progress_position_set = false;
    progress_shown_sec = -1;
}
