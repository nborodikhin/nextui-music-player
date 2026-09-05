#ifndef __UI_FONTS_H__
#define __UI_FONTS_H__

#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

// Initialize fonts (call once at startup)
void Fonts_load(void);

// Cleanup fonts (call at shutdown)
void Fonts_unload(void);

// Font accessors - return custom font or system fallback
TTF_Font* Fonts_getXLarge(void);  // Extra large (36pt)
TTF_Font* Fonts_getTitle(void);   // Track title (Regular large)
TTF_Font* Fonts_getArtist(void);  // Artist name (Medium)
TTF_Font* Fonts_getAlbum(void);   // Album name (Bold)
TTF_Font* Fonts_getLarge(void);   // General large (time display)
TTF_Font* Fonts_getMedium(void);  // General medium (lists)
TTF_Font* Fonts_getSmall(void);   // Badges, secondary text
TTF_Font* Fonts_getTiny(void);    // Genre, bitrate

// Open the app font at an exact pixel size, for text sized from a screen
// measurement rather than from the fixed scale.
//
// The caller owns the font and closes it with TTF_CloseFont().
TTF_Font* Fonts_open(int pixels);

// Whether this font can draw the UTF-8 character at c. Shaped as a callback:
// context is the TTF_Font*, and a NULL one answers yes to everything.
bool Fonts_hasGlyph(void* context, const char* c);

#endif
