// The rows of the lyric window, and the cut of a lyric that is wider than its
// row.
//
// This test links the objects of the desktop build, because ui_music_playing.c
// draws with SDL_ttf. It takes ui_music_playing.o from that build, thus it runs
// the code of the tree.

#include <stdio.h>
#include <string.h>
#include "test.h"
#include "ui_music_playing.h"
#include "defines.h"

TEST(test_lyric_rows_that_fit) {
    // Rows of 10 with a gap of 2: 34 pixels hold three rows and two gaps
    CHECK_EQ_INT(MusicPlaying_lyricRows(34, 10, 2), 3);
    // One more pixel is not a fourth row
    CHECK_EQ_INT(MusicPlaying_lyricRows(35, 10, 2), 3);
    CHECK_EQ_INT(MusicPlaying_lyricRows(46, 10, 2), 4);
    // Less than one row holds no row
    CHECK_EQ_INT(MusicPlaying_lyricRows(9, 10, 2), 0);
    CHECK_EQ_INT(MusicPlaying_lyricRows(10, 10, 2), 1);
    CHECK_EQ_INT(MusicPlaying_lyricRows(-5, 10, 2), 0);
    CHECK_EQ_INT(MusicPlaying_lyricRows(30, 0, 2), 0);
}

static SDL_Surface *make_screen(int w, int h) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s) SDL_FillRect(s, NULL, 0);
    return s;
}

// True where any pixel of the column `x` is not transparent black
static bool column_drawn(SDL_Surface *s, int x) {
    for (int y = 0; y < s->h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch);
        if (row[x] != 0) return true;
    }
    return false;
}

TEST(test_a_wide_lyric_is_cut_at_the_row) {
    if (TTF_WasInit() == 0 && TTF_Init() != 0) {
        printf("    SKIP no TTF: %s\n", TTF_GetError());
        return;
    }
    TTF_Font *font = TTF_OpenFont("../NextUI/workspace/all/show2/"
                                  "RoundedMplus1c-Bold-reduced.ttf", 18);
    if (!font) {
        printf("    SKIP no font: %s\n", TTF_GetError());
        return;
    }

    const char *text = "a lyric that is far wider than the row that holds it, and then some";
    int text_w = 0, text_h = 0;
    TTF_SizeUTF8(font, text, &text_w, &text_h);
    int max_w = text_w / 3;
    int x = 4;

    SDL_Surface *screen = make_screen(text_w + x, text_h * 3);
    CHECK(screen != NULL);
    if (!screen) { TTF_CloseFont(font); return; }

    //noinspection HardcodedColor
    SDL_Color color = {255, 255, 255, 255};
    MusicPlaying_drawLyricRow(screen, font, text, color, x, 0, max_w);

    // The text is in the row, and it stops at its width
    CHECK(column_drawn(screen, x + 2));
    bool past_the_cut = false;
    for (int col = x + max_w; col < screen->w; col++) {
        if (column_drawn(screen, col)) past_the_cut = true;
    }
    CHECK(!past_the_cut);

    // Nothing is on a second row
    bool second_row = false;
    for (int y = text_h; y < screen->h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)screen->pixels + y * screen->pitch);
        for (int col = 0; col < screen->w; col++) {
            if (row[col] != 0) second_row = true;
        }
    }
    CHECK(!second_row);

    SDL_FreeSurface(screen);
    TTF_CloseFont(font);
}

int main(void) {
    RUN(test_lyric_rows_that_fit);
    RUN(test_a_wide_lyric_is_cut_at_the_row);
    return test_summary();
}
