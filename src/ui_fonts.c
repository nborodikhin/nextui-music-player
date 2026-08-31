#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "ui_fonts.h"
#include "utf8.h"

// Path to app's bundled font
#define APP_FONT_PATH "res/font.ttf"

// Font sizes
#define FONT_TITLE_SIZE 28
#define FONT_XLARGE_SIZE 36

// App fonts at various sizes
typedef struct {
    TTF_Font* xlarge;  // 36pt
    TTF_Font* title;   // 28pt
    TTF_Font* large;   // 16pt
    TTF_Font* medium;  // 14pt
    TTF_Font* small;   // 12pt
    TTF_Font* tiny;    // 10pt
} AppFonts;

static AppFonts app_font = {0};

void Fonts_load(void) {
    app_font.xlarge = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_XLARGE_SIZE));
    app_font.title = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_TITLE_SIZE));
    app_font.large = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_LARGE));
    app_font.medium = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_MEDIUM));
    app_font.small = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_SMALL));
    app_font.tiny = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_TINY));
}

bool Fonts_hasGlyph(void* context, const char* c) {
    TTF_Font* font = (TTF_Font*)context;
    if (!font) return true;

    // Every character the maps use is in the BMP, so the 16-bit lookup covers
    // them; the device's SDL_ttf is too old for the 32-bit one
    Uint32 code = UTF8_codepoint(c);
    if (code > 0xFFFF) return false;
    return TTF_GlyphIsProvided(font, (Uint16)code) != 0;
}

void Fonts_unload(void) {
    if (app_font.xlarge) { TTF_CloseFont(app_font.xlarge); app_font.xlarge = NULL; }
    if (app_font.title) { TTF_CloseFont(app_font.title); app_font.title = NULL; }
    if (app_font.large) { TTF_CloseFont(app_font.large); app_font.large = NULL; }
    if (app_font.medium) { TTF_CloseFont(app_font.medium); app_font.medium = NULL; }
    if (app_font.small) { TTF_CloseFont(app_font.small); app_font.small = NULL; }
    if (app_font.tiny) { TTF_CloseFont(app_font.tiny); app_font.tiny = NULL; }
}

// Font accessors
TTF_Font* Fonts_getXLarge(void) { return app_font.xlarge; }
TTF_Font* Fonts_getTitle(void) { return app_font.title; }
TTF_Font* Fonts_getArtist(void) { return app_font.medium; }
TTF_Font* Fonts_getAlbum(void) { return app_font.small; }
TTF_Font* Fonts_getLarge(void) { return app_font.large; }
TTF_Font* Fonts_getMedium(void) { return app_font.medium; }
TTF_Font* Fonts_getSmall(void) { return app_font.small; }
TTF_Font* Fonts_getTiny(void) { return app_font.tiny; }

TTF_Font* Fonts_open(int pixels) {
    if (pixels < 1) pixels = 1;
    return TTF_OpenFont(APP_FONT_PATH, pixels);
}

// Get text color for list items based on selection state
SDL_Color Fonts_getListTextColor(bool selected) {
    return selected ? uintToColour(THEME_COLOR5_255) : uintToColour(THEME_COLOR4_255);
}

// Draw list item background pill (only draws if selected)
void Fonts_drawListItemBg(SDL_Surface* screen, SDL_Rect* rect, bool selected) {
    if (selected) {
        GFX_blitPillColor(ASSET_WHITE_PILL, screen, rect, THEME_COLOR1, RGB_WHITE);
    }
}

// Calculate pill width for list items
int Fonts_calcListPillWidth(TTF_Font* font, const char* text, char* truncated, int max_width, int prefix_width) {
    int available_width = max_width - prefix_width;
    int padding = SCALE1(BUTTON_PADDING * 2);

    // Check if text fits without truncation
    int raw_text_w, raw_text_h;
    TTF_SizeUTF8(font, text, &raw_text_w, &raw_text_h);

    if (raw_text_w + padding > available_width) {
        // Text needs truncation - extend pill to full width (no right padding gap)
        GFX_truncateText(font, text, truncated, available_width, padding);
        return max_width;
    }

    // Text fits - use actual text width with padding
    strncpy(truncated, text, 255);
    truncated[255] = '\0';
    return MIN(max_width, prefix_width + raw_text_w + padding);
}
