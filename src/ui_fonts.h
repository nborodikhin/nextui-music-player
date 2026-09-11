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

// A metric of a font, in pixels.
//
// The first three come from a glyph of the font, and each measures up from the
// baseline: https://en.wikipedia.org/wiki/X-height
//
// The rest come from SDL_ttf and keep the value and the sign that it returns. The
// descent is negative, because it falls below the baseline.
typedef enum {
    FONT_METRIC_X_HEIGHT,      // the top of a lowercase letter with no ascender
    FONT_METRIC_CAP_HEIGHT,    // the top of a capital letter
    FONT_METRIC_DIGIT_HEIGHT,  // the top of a digit, which has no descender
    FONT_METRIC_ASCENT,        // the baseline to the top of the font
    FONT_METRIC_DESCENT,       // the baseline to the foot of the font, negative
    FONT_METRIC_HEIGHT,        // the whole of one line, the em box
    FONT_METRIC_LINE_SKIP,     // the baseline of one line to the baseline of the next
} FontMetric;

// Returns the metric of `font` in pixels. Returns 0 where there is no font, and 0
// for a glyph metric where the font has no ascent to work from.
//
// A mark that shares a row with text takes the x-height for its thickness, thus
// each mark of the app has one weight. It sits on the metric of the text beside
// it: the x-height for a row of words, the digit height for a row of figures.
// The em box of a font holds room for an ascender, a descender and the leading,
// thus a mark that takes the em box is too tall and it reads high.
//
// Some fonts of the platform report a glyph that reaches the ascender line. Such
// a metric is not the height of that glyph, thus this takes a part of the ascent
// in its place.
int Fonts_getMetric(TTF_Font* font, FontMetric metric);

// Open the app font at an exact pixel size, for text sized from a screen
// measurement rather than from the fixed scale.
//
// The caller owns the font and closes it with TTF_CloseFont().
TTF_Font* Fonts_open(int pixels);

// Whether this font can draw the UTF-8 character at c. Shaped as a callback:
// context is the TTF_Font*, and a NULL one answers yes to everything.
bool Fonts_hasGlyph(void* context, const char* c);

#endif
