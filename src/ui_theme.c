#ifdef DEBUG
#include <assert.h>
#endif

#include "defines.h"
#include "api.h"
#include "config.h"
#include "ui_theme.h"

// Mix the progress fill into the background of the bar for the track. A light
// palette with a mid-tone accent needs less of the fill to keep the two parts
// apart, thus the mix starts at this value and falls until the bar reaches
// PROGRESS_MIN_CONTRAST.
#define PROGRESS_TRACK_MIX 31
#define PROGRESS_MIN_CONTRAST 3.0

// Move equal text colors toward the page to keep the line hierarchy. 40 percent
// of white gives 0x999999, which is the grey the app drew before it had roles.
#define SECONDARY_MIX 40

// A palette can give a surface color that a content role cannot sit on. The
// MinUI palette gives a mid grey, which the secondary role matches exactly,
// thus each hint on a dialog would be invisible. Where the palette surface
// hides a role, take the surface from the page instead.
#define SURFACE_TINT_MIX 12
#define SURFACE_MIN_CONTRAST 3.0

// A status role must reach this against its background to carry its meaning.
#define STATUS_MIN_CONTRAST 4.5

typedef struct {
    uint32_t packed;
    SDL_Color color;
} ThemeColorPair;

// [role][selected]
static ThemeColorPair role_colors[THEME_ROLE_COUNT][2];
#ifdef DEBUG
static int theme_initialized;
#endif

static void put(SDL_PixelFormat* format, ThemeRole role, bool selected, SDL_Color color) {
    role_colors[role][selected].packed =
        SDL_MapRGBA(format, color.r, color.g, color.b, color.a);
    role_colors[role][selected].color = color;
}

static SDL_Color get(ThemeRole role, bool selected) {
    return role_colors[role][selected].color;
}

// Percent of the fill to mix into the background of a bar so that the two parts
// stay apart. Less of the fill gives more contrast, thus the search falls.
static int progress_track_mix(SDL_Color background, SDL_Color fill) {
    for (int percent = PROGRESS_TRACK_MIX; percent > 0; percent--) {
        if (Theme_contrast(fill, Theme_mix(background, fill, percent))
            >= PROGRESS_MIN_CONTRAST) {
            return percent;
        }
    }
    return 0;
}

static bool surface_hides_content(SDL_Color surface, SDL_Color content) {
    return Theme_contrast(surface, content) < SURFACE_MIN_CONTRAST;
}

void Theme_init(SDL_Surface* screen) {
#ifdef DEBUG
    assert(!theme_initialized);
#endif
    SDL_PixelFormat* format = screen->format;
    SDL_Color list_text = uintToColour(CFG_getColor(COLOR_LIST_TEXT));
    SDL_Color hint = uintToColour(CFG_getColor(COLOR_HINT));
    SDL_Color page = uintToColour(CFG_getColor(COLOR_BACKGROUND));
    SDL_Color selection = uintToColour(CFG_getColor(COLOR_MAIN));
    SDL_Color selected_text = uintToColour(CFG_getColor(COLOR_LIST_TEXT_SELECTED));

    // The selected color of the page is the pill behind the row.
    put(format, THEME_ROLE_PAGE_BACKGROUND, false, page);
    put(format, THEME_ROLE_PAGE_BACKGROUND, true, selection);

    SDL_Color band = uintToColour(CFG_getColor(COLOR_ACCENT));
    put(format, THEME_ROLE_BAND_BACKGROUND, false, band);
    put(format, THEME_ROLE_BAND_BACKGROUND, true, band);

    put(format, THEME_ROLE_PRIMARY, false, list_text);
    put(format, THEME_ROLE_PRIMARY, true, selected_text);

    // One color cannot carry two levels of text. Move the secondary one toward
    // the page. A selection pill has no room for a second level at all, thus
    // the selected color stays with the primary one.
    SDL_Color secondary = hint;
    if (hint.r == list_text.r && hint.g == list_text.g && hint.b == list_text.b) {
        secondary = Theme_mix(hint, page, SECONDARY_MIX);
    }
    put(format, THEME_ROLE_SECONDARY, false, secondary);
    put(format, THEME_ROLE_SECONDARY, true, selected_text);

    // A bar on a selection pill cannot use the fill of the page, because both
    // come from COLOR_MAIN and no part of the bar would be visible. The text
    // color of a selected row already reads on that pill, thus the bar takes it.
    put(format, THEME_ROLE_PROGRESS_FILL, false, selection);
    put(format, THEME_ROLE_PROGRESS_FILL, true, selected_text);
    put(format, THEME_ROLE_PROGRESS_TRACK, false,
        Theme_mix(page, selection, progress_track_mix(page, selection)));
    put(format, THEME_ROLE_PROGRESS_TRACK, true,
        Theme_mix(selection, selected_text,
                  progress_track_mix(selection, selected_text)));

    SDL_Color surface = uintToColour(CFG_getColor(COLOR_ACCENT2));
    if (surface_hides_content(surface, list_text)
        || surface_hides_content(surface, secondary)) {
        surface = Theme_mix(page, list_text, SURFACE_TINT_MIX);
    }
    put(format, THEME_ROLE_SURFACE_BACKGROUND, false, surface);
    put(format, THEME_ROLE_SURFACE_BACKGROUND, true, surface);

    // A status role keeps its hue against each background, thus a selected row
    // still shows an error as an error.
    put(format, THEME_ROLE_STATUS_ERROR, false,
        Theme_findLegibleHue(0, STATUS_MIN_CONTRAST, page));
    put(format, THEME_ROLE_STATUS_ERROR, true,
        Theme_findLegibleHue(0, STATUS_MIN_CONTRAST, selection));
    put(format, THEME_ROLE_STATUS_SUCCESS, false,
        Theme_findLegibleHue(120, STATUS_MIN_CONTRAST, page));
    put(format, THEME_ROLE_STATUS_SUCCESS, true,
        Theme_findLegibleHue(120, STATUS_MIN_CONTRAST, selection));
#ifdef DEBUG
    theme_initialized = true;
#endif
}

uint32_t Theme_getPackedColor(ThemeRole role, bool selected) {
#ifdef DEBUG
    assert(theme_initialized);
#endif
    // Layer surfaces use the screen ARGB8888 format.
    return role_colors[role][selected].packed;
}

SDL_Color Theme_getColor(ThemeRole role, bool selected) {
#ifdef DEBUG
    assert(theme_initialized);
#endif
    return get(role, selected);
}
