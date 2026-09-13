#ifndef __UI_PODCAST_PLAYING_H__
#define __UI_PODCAST_PLAYING_H__

#include <SDL2/SDL.h>
#include <stdbool.h>

// The podcast playing screen: its renderer and the GPU layers that it paints.
// The status layer holds the row of the play time and the fill of the progress
// bar. The animation layer holds the spectrum and the marquee of the episode
// title. The regular surface holds the rest: the artwork, the metadata, the
// description and the track of the progress bar.

// Renders the screen on the surface, and records where each layer element goes.
void render_podcast_playing(SDL_Surface* screen, int show_setting,
                            int feed_index, int episode_index);

// Paints each layer of the screen. Call after each render of the surface, thus
// the frame that presents the surface holds every element.
void PodcastPlaying_paintLayers(void);

// One frame of the layers. Advances the spectrum and the marquee, and paints
// each layer that changed since its last paint where `surface_renders` is
// false: a frame that renders the surface paints every layer after that render
// instead. Returns true where the surface needs a render: the marquee of the
// title starts.
bool PodcastPlaying_frame(bool surface_renders);

// Records that the display lost the layers, thus the next frame paints each
// one again whatever its cache holds. Call it from the display-recreated
// callback.
void PodcastPlaying_invalidate(void);

// Clears the layers of the screen, forgets where their elements go, and frees
// the artwork. Call on the way out of the screen, and before the screen goes
// dark.
void PodcastPlaying_leave(void);

#endif
