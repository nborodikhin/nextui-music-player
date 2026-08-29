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

// Font at an exact pixel size, for text sized from a screen measurement rather
// than from the fixed scale.
//
// Obtained font should not be freed, and may be invalidated by a subsequent call
// to Fonts_getSized() with a different size.
TTF_Font* Fonts_getSized(int pixels);

// Theme color helpers for list items (follows system appearance)
SDL_Color Fonts_getListTextColor(bool selected);
void Fonts_drawListItemBg(SDL_Surface* screen, SDL_Rect* rect, bool selected);

// Calculate pill width for list items
// prefix_width: width of any prefix elements (indicator, checkbox, status) - pass 0 for simple text
// Returns the calculated pill width and fills truncated buffer with the truncated text
int Fonts_calcListPillWidth(TTF_Font* font, const char* text, char* truncated, int max_width, int prefix_width);

#endif
