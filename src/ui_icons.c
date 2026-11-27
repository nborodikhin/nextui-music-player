#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "ui_icons.h"

// The file of each icon, relative to the root of the pak, in the order of IconId
static const char* const icon_paths[ICON_COUNT] = {
    "res/icon-folder.png",
    "res/icon-play-all.png",
    "res/icon-audio.png",
    "res/icon-mp3.png",
    "res/icon-flac.png",
    "res/icon-ogg.png",
    "res/icon-wav.png",
    "res/icon-m4a.png",
    "res/icon-aac.png",
    "res/icon-ops.png",
    "res/icon-complete.png",
    "res/icon-download.png",
    "res/icon-empty.png",
};

static SDL_Surface* icons[ICON_COUNT];
static bool icons_loaded = false;

static void whiten_surface(SDL_Surface* surface) {
    if (!surface) return;

    SDL_LockSurface(surface);
    for (int y = 0; y < surface->h; y++) {
        Uint32* pixels = (Uint32*)((Uint8*)surface->pixels + y * surface->pitch);
        for (int x = 0; x < surface->w; x++) {
            Uint8 alpha = (pixels[x] & surface->format->Amask) >> surface->format->Ashift;
            // White source pixels let the color multiplier set the icon color.
            //noinspection HardcodedColor
            pixels[x] = SDL_MapRGBA(surface->format, 255, 255, 255, alpha);
        }
    }
    SDL_UnlockSurface(surface);
}

static SDL_Surface* load_icon(const char* path) {
    SDL_Surface* loaded = IMG_Load(path);
    if (!loaded) return NULL;
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    whiten_surface(converted);
    return converted;
}

void Icons_init(void) {
    if (icons_loaded) return;
    for (int id = 0; id < ICON_COUNT; id++) icons[id] = load_icon(icon_paths[id]);
    // The icons count as loaded when the folder icon exists
    icons_loaded = icons[ICON_FOLDER] != NULL;
}

void Icons_quit(void) {
    for (int id = 0; id < ICON_COUNT; id++) {
        if (icons[id]) SDL_FreeSurface(icons[id]);
        icons[id] = NULL;
    }
    icons_loaded = false;
}

bool Icons_isLoaded(void) {
    return icons_loaded;
}

SDL_Surface* Icons_getIcon(IconId id, SDL_Color color) {
    if (!icons_loaded || (int)id < 0 || (int)id >= ICON_COUNT) return NULL;
    SDL_Surface* icon = icons[id];
    // A format icon that did not load shows as the audio icon
    if (!icon && id >= ICON_MP3 && id <= ICON_OPUS) icon = icons[ICON_AUDIO];
    if (!icon) return NULL;
    SDL_SetSurfaceColorMod(icon, color.r, color.g, color.b);
    return icon;
}

IconId Icons_getFormatIcon(AudioFormat format) {
    switch (format) {
        case AUDIO_FORMAT_MP3:  return ICON_MP3;
        case AUDIO_FORMAT_FLAC: return ICON_FLAC;
        case AUDIO_FORMAT_OGG:  return ICON_OGG;
        case AUDIO_FORMAT_WAV:  return ICON_WAV;
        case AUDIO_FORMAT_M4A:  return ICON_M4A;
        case AUDIO_FORMAT_AAC:  return ICON_AAC;
        case AUDIO_FORMAT_OPUS: return ICON_OPUS;
        default:                return ICON_AUDIO;
    }
}
