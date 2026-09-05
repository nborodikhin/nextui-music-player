#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "ui_icons.h"

// Icon paths (relative to pak root)
#define ICON_PATH "res"
#define ICON_FOLDER    ICON_PATH "/icon-folder.png"
#define ICON_AUDIO     ICON_PATH "/icon-audio.png"
#define ICON_PLAY_ALL  ICON_PATH "/icon-play-all.png"
#define ICON_MP3       ICON_PATH "/icon-mp3.png"
#define ICON_FLAC      ICON_PATH "/icon-flac.png"
#define ICON_OGG       ICON_PATH "/icon-ogg.png"
#define ICON_WAV       ICON_PATH "/icon-wav.png"
#define ICON_M4A       ICON_PATH "/icon-m4a.png"
#define ICON_AAC       ICON_PATH "/icon-aac.png"
#define ICON_OPUS      ICON_PATH "/icon-ops.png"
// Podcast badge icons
#define ICON_COMPLETE      ICON_PATH "/icon-complete.png"
#define ICON_DOWNLOAD      ICON_PATH "/icon-download.png"
#define ICON_EMPTY         ICON_PATH "/icon-empty.png"

typedef struct {
    SDL_Surface* folder;
    SDL_Surface* audio;
    SDL_Surface* play_all;
    SDL_Surface* mp3;
    SDL_Surface* flac;
    SDL_Surface* ogg;
    SDL_Surface* wav;
    SDL_Surface* m4a;
    SDL_Surface* aac;
    SDL_Surface* opus;
    SDL_Surface* complete;
    SDL_Surface* download;
    SDL_Surface* empty;
    bool loaded;
} IconSet;

static IconSet icons = {0};

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

static void load_icon(const char* path, SDL_Surface** icon) {
    *icon = IMG_Load(path);
    if (*icon) {
        SDL_Surface* converted = SDL_ConvertSurfaceFormat(*icon, SDL_PIXELFORMAT_RGBA32, 0);
        if (converted) {
            SDL_FreeSurface(*icon);
            *icon = converted;
        } else {
            SDL_FreeSurface(*icon);
            *icon = NULL;
            return;
        }
        whiten_surface(*icon);
    }
}

// Initialize icons
void Icons_init(void) {
    if (icons.loaded) return;

    load_icon(ICON_FOLDER, &icons.folder);
    load_icon(ICON_AUDIO, &icons.audio);
    load_icon(ICON_PLAY_ALL, &icons.play_all);
    load_icon(ICON_MP3, &icons.mp3);
    load_icon(ICON_FLAC, &icons.flac);
    load_icon(ICON_OGG, &icons.ogg);
    load_icon(ICON_WAV, &icons.wav);
    load_icon(ICON_M4A, &icons.m4a);
    load_icon(ICON_AAC, &icons.aac);
    load_icon(ICON_OPUS, &icons.opus);
    load_icon(ICON_COMPLETE, &icons.complete);
    load_icon(ICON_DOWNLOAD, &icons.download);
    load_icon(ICON_EMPTY, &icons.empty);

    // Consider loaded if at least folder icon exists
    icons.loaded = (icons.folder != NULL);

    if (!icons.loaded) {
    }
}

// Cleanup icons
void Icons_quit(void) {
    if (icons.folder) { SDL_FreeSurface(icons.folder); icons.folder = NULL; }
    if (icons.audio) { SDL_FreeSurface(icons.audio); icons.audio = NULL; }
    if (icons.play_all) { SDL_FreeSurface(icons.play_all); icons.play_all = NULL; }
    if (icons.mp3) { SDL_FreeSurface(icons.mp3); icons.mp3 = NULL; }
    if (icons.flac) { SDL_FreeSurface(icons.flac); icons.flac = NULL; }
    if (icons.ogg) { SDL_FreeSurface(icons.ogg); icons.ogg = NULL; }
    if (icons.wav) { SDL_FreeSurface(icons.wav); icons.wav = NULL; }
    if (icons.m4a) { SDL_FreeSurface(icons.m4a); icons.m4a = NULL; }
    if (icons.aac) { SDL_FreeSurface(icons.aac); icons.aac = NULL; }
    if (icons.opus) { SDL_FreeSurface(icons.opus); icons.opus = NULL; }
    if (icons.complete) { SDL_FreeSurface(icons.complete); icons.complete = NULL; }
    if (icons.download) { SDL_FreeSurface(icons.download); icons.download = NULL; }
    if (icons.empty) { SDL_FreeSurface(icons.empty); icons.empty = NULL; }
    icons.loaded = false;
}

// Check if icons are loaded
bool Icons_isLoaded(void) {
    return icons.loaded;
}

static SDL_Surface* color_icon(SDL_Surface* icon, ThemeRole role, bool selected) {
    if (!icons.loaded || !icon) return NULL;

    SDL_Color color = Theme_getColor(role, selected);
    SDL_SetSurfaceColorMod(icon, color.r, color.g, color.b);
    return icon;
}

SDL_Surface* Icons_getFolder(ThemeRole role, bool selected) {
    return color_icon(icons.folder, role, selected);
}

SDL_Surface* Icons_getPlayAll(ThemeRole role, bool selected) {
    return color_icon(icons.play_all, role, selected);
}

SDL_Surface* Icons_getForFormat(AudioFormat format, ThemeRole role, bool selected) {
    if (!icons.loaded) return NULL;

    SDL_Surface* icon = NULL;

    switch (format) {
        case AUDIO_FORMAT_MP3:
            icon = icons.mp3;
            break;
        case AUDIO_FORMAT_FLAC:
            icon = icons.flac;
            break;
        case AUDIO_FORMAT_OGG:
            icon = icons.ogg;
            break;
        case AUDIO_FORMAT_WAV:
            icon = icons.wav;
            break;
        case AUDIO_FORMAT_M4A:
            icon = icons.m4a;
            break;
        case AUDIO_FORMAT_AAC:
            icon = icons.aac;
            break;
        case AUDIO_FORMAT_OPUS:
            icon = icons.opus;
            break;
        default:
            icon = icons.audio;
            break;
    }

    if (!icon) icon = icons.audio;

    return color_icon(icon, role, selected);
}

SDL_Surface* Icons_getComplete(ThemeRole role, bool selected) {
    return color_icon(icons.complete, role, selected);
}

SDL_Surface* Icons_getDownload(ThemeRole role, bool selected) {
    return color_icon(icons.download, role, selected);
}

SDL_Surface* Icons_getEmpty(ThemeRole role, bool selected) {
    return color_icon(icons.empty, role, selected);
}
