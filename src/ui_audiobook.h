#ifndef __UI_AUDIOBOOK_H__
#define __UI_AUDIOBOOK_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "audiobook.h"

// Shares GPU layer 3 with LAYER_PODCAST_PROGRESS / LAYER_PLAYTIME. The three
// modules are mutually exclusive, but each must clear the layer on the way out.
#define LAYER_AUDIOBOOK_PROGRESS 3

// Library list — books, in-progress ones first
void render_audiobook_library(SDL_Surface* screen, int show_setting,
                              int selected, int* scroll);

// Chapter list of one book
void render_audiobook_chapters(SDL_Surface* screen, int show_setting,
                               const Audiobook* book, int selected, int* scroll,
                               int playing_chapter);

// Now-playing screen. sleep_label is NULL when no sleep timer is armed.
void render_audiobook_playing(SDL_Surface* screen, int show_setting,
                              const Audiobook* book, int chapter_index,
                              const char* sleep_label);

// Marquee helpers, mirroring the podcast module's scroll handling
bool AudiobookUI_isTitleScrolling(void);
bool AudiobookUI_titleScrollNeedsRender(void);
void AudiobookUI_animateTitleScroll(void);
void AudiobookUI_clearTitleScroll(void);

// Drop every GPU layer this module owns (call when leaving the playing screen)
void AudiobookUI_clearPlayingScreen(void);

// Progress bar drawn straight to the GPU layer, refreshed once per second
bool AudiobookProgress_needsRefresh(void);
void AudiobookProgress_renderGPU(void);
void AudiobookProgress_clear(void);

#endif
