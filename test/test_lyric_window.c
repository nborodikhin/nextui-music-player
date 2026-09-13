// The current lyric for a playback position, and the rows of the lyric window.

#include "test.h"
#include "lyric_window.h"

static const LyricLine lines[] = {
    { .time_ms = 1000, .text = "one" },
    { .time_ms = 2000, .text = "two" },
    { .time_ms = 3000, .text = "three" },
    { .time_ms = 4000, .text = "four" },
};
#define LINES ((int)(sizeof(lines) / sizeof(lines[0])))

TEST(test_no_current_lyric_before_the_first_timestamp) {
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 0), -1);
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 999), -1);
}

TEST(test_a_lyric_is_current_from_its_timestamp) {
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 1000), 0);
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 3000), 2);
}

TEST(test_a_lyric_stays_current_until_the_next_timestamp) {
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 1500), 0);
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 2999), 1);
}

TEST(test_the_last_lyric_stays_current) {
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 4000), 3);
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, LINES, 90000), 3);
}

TEST(test_no_lines_give_no_current_lyric) {
    CHECK_EQ_INT(LyricWindow_currentIndex(lines, 0, 5000), -1);
}

TEST(test_odd_rows_put_the_current_lyric_on_the_middle_row) {
    LyricWindow w = LyricWindow_layout(20, 10, 5);
    CHECK_EQ_INT(w.count, 5);
    CHECK_EQ_INT(w.current_row, 2);
    CHECK_EQ_INT(w.first, 8);
}

TEST(test_even_rows_put_the_current_lyric_on_the_upper_middle_row) {
    LyricWindow w = LyricWindow_layout(20, 10, 4);
    CHECK_EQ_INT(w.count, 4);
    CHECK_EQ_INT(w.current_row, 1);
    CHECK_EQ_INT(w.first, 9);
}

TEST(test_the_window_starts_at_the_first_lyric_near_the_start) {
    LyricWindow w = LyricWindow_layout(20, 1, 5);
    CHECK_EQ_INT(w.first, 0);
    CHECK_EQ_INT(w.current_row, 1);
    CHECK_EQ_INT(w.count, 5);
}

TEST(test_the_window_ends_at_the_last_lyric_near_the_end) {
    LyricWindow w = LyricWindow_layout(20, 19, 5);
    CHECK_EQ_INT(w.first, 15);
    CHECK_EQ_INT(w.current_row, 4);
    CHECK_EQ_INT(w.count, 5);
}

TEST(test_no_current_lyric_starts_the_window_at_the_first_lyric) {
    LyricWindow w = LyricWindow_layout(20, -1, 5);
    CHECK_EQ_INT(w.first, 0);
    CHECK_EQ_INT(w.count, 5);
    CHECK_EQ_INT(w.current_row, -1);
}

TEST(test_fewer_lyrics_than_rows_fill_the_rows_that_they_can) {
    LyricWindow w = LyricWindow_layout(3, 2, 5);
    CHECK_EQ_INT(w.first, 0);
    CHECK_EQ_INT(w.count, 3);
    CHECK_EQ_INT(w.current_row, 2);
}

TEST(test_no_lyrics_give_no_rows) {
    LyricWindow w = LyricWindow_layout(0, -1, 5);
    CHECK_EQ_INT(w.count, 0);
    CHECK_EQ_INT(w.current_row, -1);
    w = LyricWindow_layout(5, 2, 0);
    CHECK_EQ_INT(w.count, 0);
}

TEST(test_one_row_holds_the_current_lyric) {
    LyricWindow w = LyricWindow_layout(20, 7, 1);
    CHECK_EQ_INT(w.first, 7);
    CHECK_EQ_INT(w.count, 1);
    CHECK_EQ_INT(w.current_row, 0);
}

int main(void) {
    RUN(test_no_current_lyric_before_the_first_timestamp);
    RUN(test_a_lyric_is_current_from_its_timestamp);
    RUN(test_a_lyric_stays_current_until_the_next_timestamp);
    RUN(test_the_last_lyric_stays_current);
    RUN(test_no_lines_give_no_current_lyric);
    RUN(test_odd_rows_put_the_current_lyric_on_the_middle_row);
    RUN(test_even_rows_put_the_current_lyric_on_the_upper_middle_row);
    RUN(test_the_window_starts_at_the_first_lyric_near_the_start);
    RUN(test_the_window_ends_at_the_last_lyric_near_the_end);
    RUN(test_no_current_lyric_starts_the_window_at_the_first_lyric);
    RUN(test_fewer_lyrics_than_rows_fill_the_rows_that_they_can);
    RUN(test_no_lyrics_give_no_rows);
    RUN(test_one_row_holds_the_current_lyric);
    return test_summary();
}
