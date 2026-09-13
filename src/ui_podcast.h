#ifndef __UI_PODCAST_H__
#define __UI_PODCAST_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include "podcast.h"

// Podcast manage menu items (Y button menu)
typedef enum {
    PODCAST_MANAGE_SEARCH = 0,
    PODCAST_MANAGE_TOP_SHOWS,
    PODCAST_MANAGE_COUNT
} PodcastManageMenuItem;

// The lists, the summaries and the queues of the podcasts. The playing screen
// is in ui_podcast_playing.h.

// Render redesigned podcast main page (continue listening + subscriptions)
void render_podcast_main_page(SDL_Surface* screen, int show_setting,
    int selected, int* scroll);

// Clear thumbnail cache (call from Podcast_cleanup)
void Podcast_clearThumbnailCache(void);

// Lazy load one pending thumbnail from disk (call from main loop)
// Returns true if a thumbnail was loaded (caller should set dirty)
bool Podcast_loadPendingThumbnails(void);

// Render the podcast management menu (Y button opens this)
void render_podcast_manage(SDL_Surface* screen, int show_setting,
                           int menu_selected, int menu_scroll, int subscription_count);

// Render Top Shows list
void render_podcast_top_shows(SDL_Surface* screen, int show_setting,
                               int selected, int* scroll);

// Render search results
void render_podcast_search_results(SDL_Surface* screen, int show_setting,
                                    int selected, int* scroll);

// Render episode list for a feed
void render_podcast_episodes(SDL_Surface* screen, int show_setting,
                              int feed_index, int selected, int* scroll);

// Render download queue view
void render_podcast_download_queue(SDL_Surface* screen, int show_setting,
                                    int selected, int* scroll);

// Render loading screen (for fetching feed, charts, etc.)
void render_podcast_loading(SDL_Surface* screen, const char* message);

// Check if podcast title is currently scrolling (for refresh)
bool Podcast_isTitleScrolling(void);

// Check if title scroll needs a render to transition (delay phase)
bool Podcast_titleScrollNeedsRender(void);

// Animate podcast title scroll only (GPU mode, no screen redraw needed)
void Podcast_animateTitleScroll(void);

// Clear podcast title scroll state (call when selection changes)
void Podcast_clearTitleScroll(void);

// The size limit of an artwork image, as fetched from the network
#define PODCAST_ARTWORK_MAX_SIZE (1024 * 1024)

// True where the image data is complete: a JPEG ends with its end marker and a
// PNG with its IEND chunk. A partial download fails to decode.
bool Podcast_imageIsComplete(const uint8_t* data, int size);

// Returns `src` in the ARGB8888 format, for a scale with no loss of the alpha
// channel. Frees `src`. Returns NULL where `src` is NULL.
SDL_Surface* Podcast_surfaceToArgb8888(SDL_Surface* src);

// Formats `seconds` as "MM:SS", or as "H:MM:SS" from one hour. A duration of
// zero or less gives "--:--".
void Podcast_formatDuration(char* buf, int seconds);

#endif
