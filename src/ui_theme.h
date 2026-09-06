#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include <SDL2/SDL.h>
#include <stdbool.h>

// The meaning that an element has in the interface.
// Get the color of an element from the Theme_* functions. Do not write one.
//
// There are two sets of colors, for the regular case and for the selected
// case. See the Theme_get*Color functions.

typedef enum {
    // The background of a screen, behind everything that the screen draws.
    THEME_ROLE_PAGE_BACKGROUND,
    // The band behind a whole row.
    THEME_ROLE_BAND_BACKGROUND,
    // The background of a panel such as a dialog or a toast.
    THEME_ROLE_SURFACE_BACKGROUND,

    // Content that is the primary focus of the user's attention: a title, the
    // text of a list item, the border of a dialog.
    THEME_ROLE_PRIMARY,
    // Content that supplements the primary one: a description, a hint, a date,
    // a duration.
    THEME_ROLE_SECONDARY,
    // The left part of a progress bar (completed portion).
    THEME_ROLE_PROGRESS_FILL,
    // The right part of a progress bar (not completed portion).
    THEME_ROLE_PROGRESS_TRACK,
    // Green-hued color that signifies success of an operation.
    THEME_ROLE_STATUS_SUCCESS,
    // Red-hued color that signifies failure of an operation.
    THEME_ROLE_STATUS_ERROR,

    THEME_ROLE_COUNT,
} ThemeRole;

// Resolve each role from the active NextUI theme.
// Must be called after GFX_init().
void Theme_init(SDL_Surface* screen);

// A theme role is the meaning of an element on the screen. The color of that
// element also depends on whether the element belongs to selected content.
//
// The row that the cursor is on is the usual selected content. The selected
// color of the page background is the pill behind that row.

// The color of a role, packed for the format of the screen. Give this to
// SDL_FillRect and to the pill blitters of the platform. Give true for
// "selected" where the element belongs to the row that the cursor is on.
uint32_t Theme_getPackedColor(ThemeRole role, bool selected);

// The color of a role as separate channels. Give this to the font functions and
// to SDL_SetSurfaceColorMod for an icon.
SDL_Color Theme_getColor(ThemeRole role, bool selected);

// --- ui_theme_utils.c ---

// Contrast ratio of two colors, as WCAG gives it. The value is 1.0 or more. A
// value of 1.0 means that the two colors are the same to the eye.
double Theme_contrast(SDL_Color a, SDL_Color b);

// Mix a percent of "to" into "from". A percent of 0 gives "from".
SDL_Color Theme_mix(SDL_Color from, SDL_Color to, int percent);

// Give a color of this hue that a user can read on the background. The hue
// carries the meaning, thus it does not change, and the saturation is fixed at
// THEME_STATUS_SATURATION. Only the lightness moves, and only until the color
// reaches min_contrast against the background. Where the start lightness
// already reaches it, the color keeps that lightness and a theme that works
// today does not change.
SDL_Color Theme_findLegibleHue(int hue, double min_contrast,
                              SDL_Color background);

#endif
