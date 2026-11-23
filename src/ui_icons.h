#ifndef __UI_ICONS_H__
#define __UI_ICONS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "player.h"  // For AudioFormat
#include "ui_theme.h"

// The icons of the app
typedef enum {
    ICON_FOLDER,
    ICON_PLAY_ALL,
    // A file of a format with no icon of its own
    ICON_AUDIO,
    ICON_MP3,
    ICON_FLAC,
    ICON_OGG,
    ICON_WAV,
    ICON_M4A,
    ICON_AAC,
    ICON_OPUS,
    // The badges of a podcast episode
    ICON_COMPLETE,
    ICON_DOWNLOAD,
    // The icon of an empty state
    ICON_EMPTY,
    ICON_COUNT,
} IconId;

// Load each icon and set its color channels to white.
void Icons_init(void);

// Cleanup icons
void Icons_quit(void);

// Returns the icon `id` in `color`, or NULL when the icon did not load. Each icon has one
// surface, thus the surface is valid only until the next request of that icon: draw it first.
SDL_Surface* Icons_getIcon(IconId id, SDL_Color color);

// Returns the icon of an audio file of `format`.
IconId Icons_getFormatIcon(AudioFormat format);

// Check if icons are loaded
bool Icons_isLoaded(void);

#ifdef __cplusplus
}
#endif

#endif // __UI_ICONS_H__
