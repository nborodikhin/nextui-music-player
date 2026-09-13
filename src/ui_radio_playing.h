#ifndef __UI_RADIO_PLAYING_H__
#define __UI_RADIO_PLAYING_H__

#include <SDL2/SDL.h>
#include <stdbool.h>

// The radio playing screen: its renderer and the GPU layers that it paints.
// The status layer holds the row of the state: the bitrate and the word of the
// state. The animation layer holds the spectrum and the fill of the buffer.
// The regular surface holds the rest: the artwork, the metadata and the track
// of the buffer.

// Pass true in `waiting` while a station waits for its screen, before its stream
// starts. The playing screen then says that it connects, in place of the metadata
// of the station that played before. Pass false once the stream starts.
void RadioUI_setWaitingToStart(bool waiting);

// Renders the screen on the surface, and records where each layer element goes.
void render_radio_playing(SDL_Surface* screen, int show_setting, int radio_selected);

// Paints each layer of the screen. Call after each render of the surface, thus
// the frame that presents the surface holds every element.
void RadioPlaying_paintLayers(void);

// One frame of the layers. Advances the spectrum, and paints each layer that
// changed since its last paint where `surface_renders` is false: a frame that
// renders the surface paints every layer after that render instead. A state
// change reaches the status layer in this frame. Returns true where the
// surface needs a render: the track of the buffer comes or goes with the state.
bool RadioPlaying_frame(bool surface_renders);

// Records that the display lost the layers, thus the next frame paints each
// one again whatever its cache holds. Call it from the display-recreated
// callback.
void RadioPlaying_invalidate(void);

// Clears the layers of the screen and forgets where their elements go. Call on
// the way out of the screen, and before the screen goes dark.
void RadioPlaying_leave(void);

#endif
