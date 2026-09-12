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
#include <string.h>
#include "test.h"
#include "ui_utils.h"
#include "ui_fonts.h"
#include "config.h"

static SDL_Surface *make_screen(int w, int h) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) {
        printf("SDL_CreateRGBSurfaceWithFormat failed: %s\n", SDL_GetError());
    }
    return s;
}

// The large font of the app, or NULL with a SKIP line where the host has none.
static TTF_Font *large_font_or_skip(void) {
    if (TTF_WasInit() == 0 && TTF_Init() != 0) {
        printf("    SKIP no TTF: %s\n", TTF_GetError());
        return NULL;
    }
    if (!Fonts_getLarge()) {
        CFG_setFontFile(NULL);
        Fonts_load();
    }
    if (!Fonts_getLarge()) printf("    SKIP no font at %s\n", RES_PATH);
    return Fonts_getLarge();
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
        // The list keeps one scroll indicator above the bottom pill row, and no
        // more: the indicator takes that room, and not the margin of the footer.
        CHECK_EQ_INT(layout.list_y + layout.list_h,
                     h - SCALE1(PADDING + PILL_SIZE) - scroll_indicator_height());
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

// The foot of a screen that draws no button hint: the margin, the row of the
// screen and the margin again.
TEST(test_footer_height) {
    CHECK_EQ_INT(chip_footer_height(SCALE1(PILL_SIZE)), SCALE1(PADDING + PILL_SIZE + PADDING));
    CHECK_EQ_INT(chip_footer_height(0), SCALE1(PADDING * 2));
    // A row shorter than a pill leaves more room for the content above it.
    CHECK(chip_footer_height(SCALE1(PILL_SIZE) / 2) < chip_footer_height(SCALE1(PILL_SIZE)));
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

// The title area ends one inset before the status group, and it ends one inset
// before the right margin where the platform draws none.
TEST(test_screen_title_area) {
    SDL_Surface *screen = make_screen(1024, 768);
    CHECK(screen != NULL);
    int inset = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    int text_h = 20;

    // With the status group
    SDL_Rect a = screen_title_area(screen, 200, text_h);
    CHECK_EQ_INT(a.x, inset);
    CHECK_EQ_INT(a.x + a.w, 1024 - inset - 200);
    CHECK_EQ_INT(a.h, text_h);
    // On the middle of the top pill row
    CHECK_EQ_INT(a.y + text_h / 2, SCALE1(PADDING) + SCALE1(PILL_SIZE) / 2);

    // A setting pill is wider than the status group, thus the area shrinks
    SDL_Rect b = screen_title_area(screen, 300, text_h);
    CHECK_EQ_INT(b.x, inset);
    CHECK_EQ_INT(b.x + b.w, 1024 - inset - 300);

    // With neither
    SDL_Rect c = screen_title_area(screen, 0, text_h);
    CHECK_EQ_INT(c.x + c.w, 1024 - inset);

    SDL_FreeSurface(screen);
}

// A title longer than the buffer of a path paints at rest, and only in its
// area. Once it moves, the surface no longer holds it: the marquee is on the
// GPU layer.
TEST(test_screen_title_paint_stays_in_its_area) {
    TTF_Font *font = large_font_or_skip();
    if (!font) return;

    char text[400];
    memset(text, 'W', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';

    SDL_Surface *screen = make_screen(1024, 768);
    CHECK(screen != NULL);
    SDL_FillRect(screen, NULL, 0);

    ScreenTitle title;
    ScreenTitle_reset(&title, false);
    ScreenTitle_set(&title, text, 4000, 0);
    SDL_Rect area = screen_title_area(screen, 200, TTF_FontHeight(font));
    SDL_Color white = {255, 255, 255, 255};

    // At rest: the surface takes it
    CHECK(paint_screen_title(screen, &title, font, white, area, 0));
    CHECK(title.seg[0].text_w > area.w);

    // No pixel to the right of the area, where the status group is
    uint32_t *px = screen->pixels;
    int pitch = screen->pitch / 4;
    int painted_inside = 0, painted_outside = 0;
    for (int y = 0; y < screen->h; y++) {
        for (int x = 0; x < screen->w; x++) {
            if ((px[y * pitch + x] & 0x00ffffff) == 0) continue;
            bool inside = x >= area.x && x < area.x + area.w && y >= area.y && y < area.y + area.h;
            if (inside) painted_inside++; else painted_outside++;
        }
    }
    CHECK(painted_inside > 0);
    CHECK_EQ_INT(painted_outside, 0);

    // In movement the surface paint draws nothing, thus the layer owns the slice
    SDL_FillRect(screen, NULL, 0);
    CHECK(!paint_screen_title(screen, &title, font, white, area, SCREEN_TITLE_START_DELAY_MS + 500));
    CHECK(ScreenTitle_moves(&title, SCREEN_TITLE_START_DELAY_MS + 500));
    int painted = 0;
    for (int y = 0; y < screen->h; y++) {
        for (int x = 0; x < screen->w; x++) {
            if ((px[y * pitch + x] & 0x00ffffff) != 0) painted++;
        }
    }
    CHECK_EQ_INT(painted, 0);

    SDL_FreeSurface(screen);
}

// A menu row with an icon reserves the width of the icon one time: the title
// takes all of the row that remains after it.
TEST(test_menu_row_reserves_the_icon_once) {
    TTF_Font *font = large_font_or_skip();
    if (!font) return;

    SDL_Surface *screen = make_screen(1024, 768);
    CHECK(screen != NULL);
    ListLayout layout = calc_list_layout(screen);
    int icon = SCALE1(24) + SCALE1(6);
    int padding = SCALE1(BUTTON_PADDING * 2);
    char truncated[256];

    // A short title: the pill holds the icon, the title and the padding
    MenuItemPos pos = render_menu_item_pill(screen, &layout, "Library", truncated, 0, false, icon);
    int text_w;
    TTF_SizeUTF8(font, "Library", &text_w, NULL);
    CHECK_EQ_INT(pos.pill_width, icon + text_w + padding);
    CHECK_EQ_INT(strcmp(truncated, "Library"), 0);

    // A long title: the pill takes the row, and the title ends where the row ends,
    // thus it lost one icon and no more. The text stays under the 256 bytes of
    // `truncated`, which the platform fills with no bound.
    char text[200];
    memset(text, 'W', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    pos = render_menu_item_pill(screen, &layout, text, truncated, 1, false, icon);
    CHECK_EQ_INT(pos.pill_width, layout.max_width);
    TTF_SizeUTF8(font, truncated, &text_w, NULL);
    int room = layout.max_width - icon - padding;
    CHECK(text_w <= room);
    // One more glyph did not fit, thus the title did not stop an icon too soon
    int glyph_w;
    TTF_SizeUTF8(font, "W", &glyph_w, NULL);
    CHECK(text_w + glyph_w > room);


    SDL_FreeSurface(screen);
}

int main(void) {
    RUN(test_chip_on_the_top_pill_row);
    RUN(test_footer_chip_box);
    RUN(test_status_group_threshold);
    RUN(test_list_keeps_out_of_the_bottom_pill_row);
    RUN(test_rows_of_a_page);
    RUN(test_footer_height);
    RUN(test_header_height);
    RUN(test_screen_title_area);
    RUN(test_screen_title_paint_stays_in_its_area);
    RUN(test_menu_row_reserves_the_icon_once);
    return test_summary();
}
