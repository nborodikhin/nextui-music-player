#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "ui_fonts.h"

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

// Fonts opened at caller-chosen sizes. A screen mixes a couple of them (key
// glyphs and their smaller labels), so one slot per size in use.
#define SIZED_FONT_SLOTS 4

static TTF_Font* sized_font[SIZED_FONT_SLOTS];
static int sized_font_pixels[SIZED_FONT_SLOTS];
static int sized_font_next = 0;

void Fonts_load(void) {
    app_font.xlarge = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_XLARGE_SIZE));
    app_font.title = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_TITLE_SIZE));
    app_font.large = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_LARGE));
    app_font.medium = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_MEDIUM));
    app_font.small = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_SMALL));
    app_font.tiny = TTF_OpenFont(APP_FONT_PATH, SCALE1(FONT_TINY));
}

void Fonts_unload(void) {
    if (app_font.xlarge) { TTF_CloseFont(app_font.xlarge); app_font.xlarge = NULL; }
    if (app_font.title) { TTF_CloseFont(app_font.title); app_font.title = NULL; }
    if (app_font.large) { TTF_CloseFont(app_font.large); app_font.large = NULL; }
    if (app_font.medium) { TTF_CloseFont(app_font.medium); app_font.medium = NULL; }
    if (app_font.small) { TTF_CloseFont(app_font.small); app_font.small = NULL; }
    if (app_font.tiny) { TTF_CloseFont(app_font.tiny); app_font.tiny = NULL; }
    for (int slot = 0; slot < SIZED_FONT_SLOTS; slot++) {
        if (!sized_font[slot]) continue;
        TTF_CloseFont(sized_font[slot]);
        sized_font[slot] = NULL;
        sized_font_pixels[slot] = 0;
    }
    sized_font_next = 0;
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

TTF_Font* Fonts_getSized(int pixels) {
    if (pixels < 1) pixels = 1;

    for (int slot = 0; slot < SIZED_FONT_SLOTS; slot++) {
        if (sized_font[slot] && sized_font_pixels[slot] == pixels) return sized_font[slot];
    }

    TTF_Font* font = TTF_OpenFont(APP_FONT_PATH, pixels);
    if (!font) return NULL;

    // Slots are taken in turn, so the sizes of one screen do not evict each other
    int slot = sized_font_next;
    sized_font_next = (sized_font_next + 1) % SIZED_FONT_SLOTS;
    if (sized_font[slot]) TTF_CloseFont(sized_font[slot]);
    sized_font[slot] = font;
    sized_font_pixels[slot] = pixels;
    return font;
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
