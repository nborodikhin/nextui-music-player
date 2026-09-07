#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "ui_fonts.h"
#include "utf8.h"

// The font of NextUI. CFG_getFontFile() gives the name of the file that the
// user selected, and each font of the system is in RES_PATH.
static char font_path[MAX_PATH];

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

// Resolve the font of the system. CFG_getFontFile() cannot give NULL, because
// it returns an array of the settings struct, and CFG_setFontFile() goes back to
// the default where the file that the user selected is not there.
static void resolve_font_path(void) {
    snprintf(font_path, sizeof(font_path), "%s/%s", RES_PATH, CFG_getFontFile());
}

// Open the font of the system at one size, in the style that the user
// selected. NextUI gives each of its own fonts that style, thus a font that
// misses it draws lighter than every other screen of the device.
static TTF_Font* open_styled(int pixels) {
    TTF_Font* font = TTF_OpenFont(font_path, pixels);
    if (font) TTF_SetFontStyle(font, CFG_getFontStyle());
    return font;
}

void Fonts_load(void) {
    resolve_font_path();
    app_font.xlarge = open_styled(SCALE1(FONT_XLARGE_SIZE));
    app_font.title = open_styled(SCALE1(FONT_TITLE_SIZE));
    app_font.large = open_styled(SCALE1(FONT_LARGE));
    app_font.medium = open_styled(SCALE1(FONT_MEDIUM));
    app_font.small = open_styled(SCALE1(FONT_SMALL));
    app_font.tiny = open_styled(SCALE1(FONT_TINY));
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
    return open_styled(pixels);
}

