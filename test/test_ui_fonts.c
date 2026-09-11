// The metrics of a font that `ui_fonts.c` gives.
//
// This test links the objects of the desktop build, because ui_fonts.c reads the
// theme of the platform. The objects come from the same sources as the app, thus
// the test runs the font code of the tree.

#include <stdio.h>
#include "test.h"
#include "ui_fonts.h"
#include "defines.h"

// The metrics of a font, in pixels above the baseline. The x-height is the height
// of a lowercase letter with no ascender; a digit is taller and has no descender.
// Both are shorter than the em box, which holds room for an ascender, a descender
// and the leading of the line.
TEST(test_font_metrics) {
    // With no font the caller still gets a value that it can draw with
    CHECK_EQ_INT(Fonts_getMetric(NULL, FONT_METRIC_X_HEIGHT), 0);
    CHECK_EQ_INT(Fonts_getMetric(NULL, FONT_METRIC_DIGIT_HEIGHT), 0);
    CHECK_EQ_INT(Fonts_getMetric(NULL, FONT_METRIC_ASCENT), 0);
    CHECK_EQ_INT(Fonts_getMetric(NULL, FONT_METRIC_LINE_SKIP), 0);

    if (TTF_WasInit() == 0 && TTF_Init() != 0) {
        printf("    SKIP no TTF: %s\n", TTF_GetError());
        return;
    }

    TTF_Font *font = TTF_OpenFont("../NextUI/workspace/all/show2/"
                                  "RoundedMplus1c-Bold-reduced.ttf", SCALE1(18));
    if (!font) {
        printf("    SKIP no font: %s\n", TTF_GetError());
        return;
    }

    int x_height = Fonts_getMetric(font, FONT_METRIC_X_HEIGHT);
    int digit_height = Fonts_getMetric(font, FONT_METRIC_DIGIT_HEIGHT);
    int line_h = TTF_FontHeight(font);
    int ascent = TTF_FontAscent(font);

    CHECK(x_height > 0);

    int cap_height = Fonts_getMetric(font, FONT_METRIC_CAP_HEIGHT);

    // A capital and a digit are taller than a lowercase letter, and each stays
    // under the ascent
    CHECK(cap_height > x_height);
    CHECK(digit_height > x_height);
    CHECK(cap_height <= ascent);
    CHECK(digit_height <= ascent);

    // Both are shorter than the em box, which is the fault that this replaces
    CHECK(digit_height < line_h);

    // The metrics of SDL come back as SDL gives them
    CHECK_EQ_INT(Fonts_getMetric(font, FONT_METRIC_ASCENT), ascent);
    CHECK_EQ_INT(Fonts_getMetric(font, FONT_METRIC_HEIGHT), line_h);
    CHECK_EQ_INT(Fonts_getMetric(font, FONT_METRIC_LINE_SKIP), TTF_FontLineSkip(font));

    // The descent falls below the baseline, thus it is negative
    CHECK(Fonts_getMetric(font, FONT_METRIC_DESCENT) < 0);
    CHECK_EQ_INT(Fonts_getMetric(font, FONT_METRIC_DESCENT), TTF_FontDescent(font));

    // The whole of a line holds the ascent and the descent
    CHECK_EQ_INT(ascent - Fonts_getMetric(font, FONT_METRIC_DESCENT), line_h);

    TTF_CloseFont(font);
}

int main(void) {
    RUN(test_font_metrics);
    return test_summary();
}
