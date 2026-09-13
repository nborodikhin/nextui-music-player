#ifndef __UI_MUSIC_PLAYING_H__
#define __UI_MUSIC_PLAYING_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "browser.h"

// The music playing screen: its renderer and the GPU layers that it paints.
// The status layer holds the play time. The animation layer holds the spectrum
// and the marquee of the title. The regular surface holds the rest: the album
// art, the metadata, the lyric window and the playback mode indicators.

// Renders the screen on the surface, and records where each layer element goes.
// playlist_track_num and playlist_total: if > 0, use these instead of browser counts
void render_playing(SDL_Surface* screen, int show_setting, BrowserContext* browser,
                    bool shuffle_enabled, bool repeat_enabled,
                    int playlist_track_num, int playlist_total);

// Paints each layer of the screen. Call after each render of the surface, thus
// the frame that presents the surface holds every element.
void MusicPlaying_paintLayers(void);

// One frame of the layers. Advances the spectrum and the marquee, and paints
// each layer that changed since its last paint where `surface_renders` is
// false: a frame that renders the surface paints every layer after that render
// instead. Returns true where the surface needs a render: the current lyric or
// the lyric availability changed, or the marquee of the title starts.
bool MusicPlaying_frame(bool surface_renders);

// Records that the display lost the layers, thus the next frame paints each
// one again whatever its cache holds. Call it from the display-recreated
// callback.
void MusicPlaying_invalidate(void);

// Clears the layers of the screen and forgets where their elements go. Call on
// the way out of the screen, and before the screen goes dark. The loaded lyrics
// stay with the track.
void MusicPlaying_leave(void);

// Draws `text` on one row of the lyric window at `x`, `y`. A text wider than
// `max_w` is cut at that width and takes no second row.
void MusicPlaying_drawLyricRow(SDL_Surface* screen, TTF_Font* font, const char* text,
                               SDL_Color color, int x, int y, int max_w);

// Returns the count of lyric rows that fit in `height` pixels with rows `row_h`
// high and `gap` pixels between two rows.
int MusicPlaying_lyricRows(int height, int row_h, int gap);

#endif
