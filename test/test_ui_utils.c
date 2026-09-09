// Geometry of the pill rows and of the list layout.
//
// This test links SDL and the objects of the desktop build, because ui_utils.c
// draws and thus includes the platform. The other tests here compile a source
// with no platform include. This one cannot, and it needs no stub for that
// reason: it takes the objects that the desktop build already made.
//
// The scale and the padding come from the desktop platform header, thus one run
// gives one pair of those two. The size of the screen is a parameter, thus the
// four sizes below all run.

#include <stdio.h>
#include "test.h"
#include "ui_utils.h"

static SDL_Surface *make_screen(int w, int h) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) {
        printf("SDL_CreateRGBSurfaceWithFormat failed: %s\n", SDL_GetError());
    }
    return s;
}

TEST(test_chip_on_the_top_pill_row) {
    SDL_Surface *wide = make_screen(SCALE1(320), 480);
    SDL_Surface *narrow = make_screen(SCALE1(320) - 1, 480);
    CHECK(wide != NULL);
    CHECK(narrow != NULL);

    // With the status group the chip centers on the pill row.
    CHECK_EQ_INT(top_of_the_chip_box(wide, SCALE1(PILL_SIZE)), SCALE1(PADDING));
    CHECK_EQ_INT(top_of_the_chip_box(wide, 0), SCALE1(PADDING) + SCALE1(PILL_SIZE) / 2);
    int box = SCALE1(PILL_SIZE) / 3;
    CHECK_EQ_INT(top_of_the_chip_box(wide, box) + box / 2,
                 SCALE1(PADDING) + SCALE1(PILL_SIZE) / 2);

    // With none it takes the top margin, thus its gap to the top edge and its gap to
    // the left edge are the same.
    CHECK_EQ_INT(top_of_the_chip_box(narrow, box), SCALE1(PADDING));

    SDL_FreeSurface(wide);
    SDL_FreeSurface(narrow);
}

TEST(test_footer_chip_box) {
    const int sizes[][2] = {{1024, 768}, {1280, 720}, {640, 480}, {320, 240}};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        SDL_Surface *screen = make_screen(sizes[i][0], sizes[i][1]);
        CHECK(screen != NULL);
        int h = sizes[i][1];

        // The foot of the box sits on the bottom margin, whatever the box holds.
        CHECK_EQ_INT(top_of_the_footer_chip_box(screen, 0), h - SCALE1(PADDING));
        int box = SCALE1(PILL_SIZE) / 3;
        CHECK_EQ_INT(top_of_the_footer_chip_box(screen, box) + box, h - SCALE1(PADDING));
        // A box as high as the pill reaches the top of the bottom pill row.
        CHECK_EQ_INT(top_of_the_footer_chip_box(screen, SCALE1(PILL_SIZE)),
                     h - SCALE1(PADDING + PILL_SIZE));

        SDL_FreeSurface(screen);
    }
}

TEST(test_status_group_threshold) {
    SDL_Surface *wide = make_screen(SCALE1(320), 480);
    SDL_Surface *narrow = make_screen(SCALE1(320) - 1, 480);
    CHECK(wide != NULL);
    CHECK(narrow != NULL);

    CHECK(screen_has_status_group(wide));
    CHECK(!screen_has_status_group(narrow));

    SDL_FreeSurface(wide);
    SDL_FreeSurface(narrow);
}

// No row of a list reaches the bottom pill row, which holds the button hints.
TEST(test_list_keeps_out_of_the_bottom_pill_row) {
    const int sizes[][2] = {{1024, 768}, {1280, 720}, {640, 480}, {320, 240}};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        SDL_Surface *screen = make_screen(sizes[i][0], sizes[i][1]);
        CHECK(screen != NULL);
        int h = sizes[i][1];
        ListLayout layout = calc_list_layout(screen);

        CHECK_EQ_INT(layout.list_y, SCALE1(PADDING + PILL_SIZE));
        CHECK_EQ_INT(layout.list_y + layout.list_h, h - pill_footer_height());
        CHECK(layout.list_y + layout.items_per_page * layout.item_h
               <= h - SCALE1(PADDING + PILL_SIZE));
        CHECK(layout.list_y + layout.rich_items_per_page * layout.rich_item_h
               <= h - SCALE1(PADDING + PILL_SIZE));
        CHECK_EQ_INT(layout.max_width, sizes[i][0] - SCALE1(PADDING * 2));

        SDL_FreeSurface(screen);
    }
}

// The count of rows of one page for each size of screen, at the scale and the
// padding that the desktop header gives, which is the geometry of the Brick. A
// change to a constant of the layout that takes a row away fails here.
TEST(test_rows_of_a_page) {
    struct { int w, h, rows, rich; } cases[] = {
        {1024, 768, 6, 4},
        {1280, 720, 5, 3},
        {640, 480, 2, 1},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SDL_Surface *screen = make_screen(cases[i].w, cases[i].h);
        CHECK(screen != NULL);
        ListLayout layout = calc_list_layout(screen);
        CHECK_EQ_INT(layout.items_per_page, cases[i].rows);
        CHECK_EQ_INT(layout.rich_items_per_page, cases[i].rich);
        SDL_FreeSurface(screen);
    }
}

// The foot of a screen: the pill row with a margin on each side, or the margin, the
// row of the screen and the margin again.
TEST(test_footer_height) {
    CHECK_EQ_INT(pill_footer_height(), SCALE1(PADDING + PILL_SIZE + PADDING));
    CHECK_EQ_INT(chip_footer_height(SCALE1(PILL_SIZE)), SCALE1(PADDING + PILL_SIZE + PADDING));
    CHECK_EQ_INT(chip_footer_height(0), SCALE1(PADDING * 2));
    // A row shorter than a pill leaves more room for the content above it.
    CHECK(chip_footer_height(SCALE1(PILL_SIZE) / 2) < pill_footer_height());
}

// The header of a playing screen keeps the pill row where the platform draws the
// status group, and the chip with a margin on each side where it draws none.
TEST(test_header_height) {
    SDL_Surface *wide = make_screen(SCALE1(320), 480);
    SDL_Surface *narrow = make_screen(SCALE1(320) - 1, 480);
    CHECK(wide != NULL);
    CHECK(narrow != NULL);
    int chip_h = SCALE1(PILL_SIZE) / 2;

    CHECK_EQ_INT(total_header_height(wide, chip_h), SCALE1(PADDING + PILL_SIZE));
    CHECK_EQ_INT(total_header_height(narrow, chip_h), SCALE1(PADDING) + chip_h + SCALE1(PADDING));
    CHECK(total_header_height(narrow, chip_h) < total_header_height(wide, chip_h));

    SDL_FreeSurface(wide);
    SDL_FreeSurface(narrow);
}

int main(void) {
    RUN(test_chip_on_the_top_pill_row);
    RUN(test_footer_chip_box);
    RUN(test_status_group_threshold);
    RUN(test_list_keeps_out_of_the_bottom_pill_row);
    RUN(test_rows_of_a_page);
    RUN(test_footer_height);
    RUN(test_header_height);
    return test_summary();
}
