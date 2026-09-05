#include "test.h"
#include "ui_theme.h"

static void check_color(SDL_Color color, int red, int green, int blue) {
    CHECK_EQ_INT(color.r, red);
    CHECK_EQ_INT(color.g, green);
    CHECK_EQ_INT(color.b, blue);
}

static void check_not_greyscale(SDL_Color color) {
    CHECK(color.r != color.g || color.g != color.b);
}

TEST(default_palette_keeps_start_lightness) {
    SDL_Color page = {
        .r = 0,
        .g = 0,
        .b = 0,
    };
    check_color(Theme_findLegibleHue(0, 4.5, page), 200, 80, 80);
    check_color(Theme_findLegibleHue(120, 4.5, page), 80, 200, 80);
}

TEST(teal_powder_moves_away_from_page) {
    SDL_Color page = {
        .r = 0xe9,
        .g = 0xf2,
        .b = 0xf5,
    };
    check_color(Theme_findLegibleHue(0, 4.5, page), 195, 63, 63);
    check_color(Theme_findLegibleHue(120, 4.5, page), 40, 126, 40);
}

TEST(mid_grey_keeps_green_hue) {
    SDL_Color page = {
        .r = 0x80,
        .g = 0x80,
        .b = 0x80,
    };
    check_not_greyscale(Theme_findLegibleHue(120, 4.5, page));
}

TEST(tan_keeps_green_hue) {
    SDL_Color page = {
        .r = 0xc8,
        .g = 0xa1,
        .b = 0x65,
    };
    check_not_greyscale(Theme_findLegibleHue(120, 4.5, page));
}

TEST(one_color_against_itself_gives_no_contrast) {
    SDL_Color grey = {
        .r = 0x99,
        .g = 0x99,
        .b = 0x99,
    };
    // The MinUI palette gives this grey for its surface, and the secondary text
    // role resolves to the same value. Theme_init() reads the ratio to find that.
    CHECK(Theme_contrast(grey, grey) < 1.001);
}

TEST(black_against_white_gives_the_full_range) {
    SDL_Color black = {.r = 0, .g = 0, .b = 0};
    SDL_Color white = {.r = 0xff, .g = 0xff, .b = 0xff};
    CHECK(Theme_contrast(black, white) > 20.9);
    CHECK(Theme_contrast(white, black) > 20.9);
}

int main(void) {
    RUN(default_palette_keeps_start_lightness);
    RUN(teal_powder_moves_away_from_page);
    RUN(mid_grey_keeps_green_hue);
    RUN(tan_keeps_green_hue);
    RUN(one_color_against_itself_gives_no_contrast);
    RUN(black_against_white_gives_the_full_range);
    return test_summary();
}
