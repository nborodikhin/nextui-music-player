#ifndef __UI_ICONS_H__
#define __UI_ICONS_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "player.h"  // For AudioFormat
#include "ui_theme.h"

// Load each icon and set its color channels to white.
void Icons_init(void);

// Cleanup icons
void Icons_quit(void);

// Icon getters. Note that each icon has a surface. To avoid collisions,
// draw the obtained surface before the next icon request.

// File type icons
SDL_Surface* Icons_getFolder(ThemeRole role, bool selected);
SDL_Surface* Icons_getPlayAll(ThemeRole role, bool selected);
SDL_Surface* Icons_getForFormat(AudioFormat format, ThemeRole role, bool selected);

// Podcast badge icons
SDL_Surface* Icons_getComplete(ThemeRole role, bool selected);
SDL_Surface* Icons_getDownload(ThemeRole role, bool selected);

// Empty state icon
SDL_Surface* Icons_getEmpty(ThemeRole role, bool selected);

// Check if icons are loaded
bool Icons_isLoaded(void);

#endif // __UI_ICONS_H__
