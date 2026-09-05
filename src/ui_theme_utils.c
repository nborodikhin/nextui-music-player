// Color arithmetic for the theme roles. Kept apart from ui_theme.c so that a
// host test can operate it, and declared in ui_theme.h rather than in a header
// of its own, because nothing outside the theme uses it.

#include <math.h>

#include "ui_theme.h"

// A status color keeps one saturation on each theme. A lower value reads as
// grey and a higher one fights the palette.
#define THEME_STATUS_SATURATION 52

// Where the search for a legible status color starts, in the byte range.
#define THEME_STATUS_START_LIGHTNESS ((55 * 255 + 50) / 100)

static double channel_luminance(uint8_t channel) {
    double value = channel / 255.0;
    if (value <= 0.04045) return value / 12.92;
    return pow((value + 0.055) / 1.055, 2.4);
}

static double relative_luminance(SDL_Color color) {
    return 0.2126 * channel_luminance(color.r) +
           0.7152 * channel_luminance(color.g) +
           0.0722 * channel_luminance(color.b);
}

double Theme_contrast(SDL_Color a, SDL_Color b) {
    double a_luminance = relative_luminance(a);
    double b_luminance = relative_luminance(b);
    double lighter = a_luminance > b_luminance ? a_luminance : b_luminance;
    double darker = a_luminance > b_luminance ? b_luminance : a_luminance;
    return (lighter + 0.05) / (darker + 0.05);
}

SDL_Color Theme_mix(SDL_Color from, SDL_Color to, int percent) {
    SDL_Color color = {
        .r = from.r + (to.r - from.r) * percent / 100,
        .g = from.g + (to.g - from.g) * percent / 100,
        .b = from.b + (to.b - from.b) * percent / 100,
        .a = from.a + (to.a - from.a) * percent / 100,
    };
    return color;
}

static uint8_t color_channel(double first, double second, double hue) {
    if (hue < 0.0) hue += 1.0;
    if (hue > 1.0) hue -= 1.0;
    if (hue < 1.0 / 6.0) return (uint8_t)lround((first + (second - first) * 6.0 * hue) * 255.0);
    if (hue < 1.0 / 2.0) return (uint8_t)lround(second * 255.0);
    if (hue < 2.0 / 3.0) return (uint8_t)lround((first + (second - first) * (2.0 / 3.0 - hue) * 6.0) * 255.0);
    return (uint8_t)lround(first * 255.0);
}

// Hue uses degrees. Saturation uses percent. Lightness uses the byte range.
static SDL_Color hsl_color(int hue, int saturation, int lightness) {
    double h = hue / 360.0;
    double s = saturation / 100.0;
    double l = lightness / 255.0;
    double second = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    double first = 2.0 * l - second;

    SDL_Color color = {
        .r = color_channel(first, second, h + 1.0 / 3.0),
        .g = color_channel(first, second, h),
        .b = color_channel(first, second, h - 1.0 / 3.0),
        .a = 255,
    };
    return color;
}

SDL_Color Theme_findLegibleHue(int hue, double min_contrast, SDL_Color background) {
    SDL_Color best = hsl_color(hue, THEME_STATUS_SATURATION, THEME_STATUS_START_LIGHTNESS);
    double best_contrast = Theme_contrast(best, background);
    if (best_contrast >= min_contrast) return best;

    // Walk the lightness away from the background first, then the other way.
    int first_direction = relative_luminance(background) > relative_luminance(best) ? -1 : 1;
    for (int pass = 0; pass < 2; pass++) {
        int direction = pass == 0 ? first_direction : -first_direction;
        for (int lightness = THEME_STATUS_START_LIGHTNESS + direction;
             lightness >= 0 && lightness <= 255;
             lightness += direction) {
            SDL_Color candidate = hsl_color(hue, THEME_STATUS_SATURATION, lightness);
            double candidate_contrast = Theme_contrast(candidate, background);
            if (candidate_contrast > best_contrast) {
                best = candidate;
                best_contrast = candidate_contrast;
            }
            if (candidate_contrast >= min_contrast) return candidate;
        }
    }
    return best;
}
