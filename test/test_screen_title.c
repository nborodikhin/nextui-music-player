// Tests for src/screen_title.c - the tape of a screen title and where its
// window is.
//
// Pins the rest of a title that fits, the start delay, the time-based
// movement, the loop of one text, a change of the area width, and the tape
// that the file browser uses: a change scrolls in after the text under the
// window, which is cut or padded there, and nothing in the window jumps.

#include <stdio.h>
#include <string.h>

#include "test.h"
#include "screen_title.h"

#define DELAY SCREEN_TITLE_START_DELAY_MS
#define SPEED SCREEN_TITLE_SPEED_PX_PER_S

// The area is 200 wide, the gap 30 and the fade 20. A text 300 wide loops in 330.
#define AREA 200
#define GAP  30
#define FADE 20

static ScreenTitle moving_title(bool defer, uint32_t now) {
    ScreenTitle t;
    ScreenTitle_reset(&t, defer);
    ScreenTitle_measure(&t, AREA, GAP, FADE, 0);
    ScreenTitle_set(&t, "A long title", 300, now);
    return t;
}

// The time at which the window has moved `px` pixels since the delay ended.
static uint32_t at_px(int px) {
    return DELAY + ((uint32_t)px * 1000 + SPEED - 1) / SPEED;
}

TEST(a_title_that_fits_stays_still) {
    ScreenTitle t;
    ScreenTitle_reset(&t, false);
    ScreenTitle_measure(&t, AREA, GAP, FADE, 0);
    CHECK(ScreenTitle_set(&t, "Settings", 100, 0));

    CHECK(!ScreenTitle_moves(&t, DELAY * 10));
    CHECK(!ScreenTitle_needsFrame(&t, DELAY * 10, true));
    CHECK_EQ_INT(ScreenTitle_offset(&t, DELAY * 10), 0);
    CHECK_EQ_INT(t.count, 1);
}

TEST(the_same_text_is_no_change) {
    ScreenTitle t;
    ScreenTitle_reset(&t, false);
    ScreenTitle_measure(&t, AREA, GAP, FADE, 0);
    CHECK(ScreenTitle_set(&t, "Settings", 100, 0));
    CHECK(!ScreenTitle_set(&t, "Settings", 100, 500));
    CHECK_EQ_INT(t.shown_at, 0);
}

TEST(a_wide_title_waits_for_the_start_delay) {
    ScreenTitle t = moving_title(false, 1000);

    CHECK(!ScreenTitle_moves(&t, 1000 + DELAY - 1));
    CHECK(!ScreenTitle_needsFrame(&t, 1000 + DELAY - 1, true));
    CHECK_EQ_INT(ScreenTitle_offset(&t, 1000 + DELAY - 1), 0);

    CHECK(ScreenTitle_moves(&t, 1000 + DELAY));
    CHECK(ScreenTitle_needsFrame(&t, 1000 + DELAY, true));
    CHECK_EQ_INT(ScreenTitle_offset(&t, 1000 + DELAY), 0);

    // Once the surface no longer shows the title, the marquee asks for no frame
    CHECK(!ScreenTitle_needsFrame(&t, 1000 + DELAY, false));
}

TEST(the_offset_follows_the_clock) {
    ScreenTitle t = moving_title(false, 0);

    CHECK_EQ_INT(ScreenTitle_offset(&t, DELAY + 1000), SPEED);
    CHECK_EQ_INT(ScreenTitle_offset(&t, DELAY + 500), SPEED / 2);
    // The rate does not depend on how often the caller asks.
    CHECK_EQ_INT(ScreenTitle_offset(&t, DELAY + 1000), SPEED);
}

TEST(one_text_loops_after_its_gap) {
    ScreenTitle t = moving_title(false, 0);

    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(329)), 329);
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(330)), 0);
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(330) + 1000), SPEED);
    CHECK_EQ_INT(t.count, 1);
}

TEST(a_wider_area_stops_the_title) {
    ScreenTitle t = moving_title(false, 0);
    CHECK(ScreenTitle_moves(&t, DELAY + 1000));

    ScreenTitle_measure(&t, 400, GAP, FADE, DELAY + 1000);
    CHECK(!ScreenTitle_moves(&t, DELAY + 1000));
    CHECK_EQ_INT(ScreenTitle_offset(&t, DELAY + 1000), 0);
}

TEST(a_new_text_replaces_the_title_at_once_without_defer) {
    ScreenTitle t = moving_title(false, 0);
    CHECK(ScreenTitle_set(&t, "Another", 100, DELAY + 1000));
    CHECK_EQ_INT(strcmp(t.seg[0].text, "Another"), 0);
    CHECK_EQ_INT(t.count, 1);
    CHECK_EQ_INT(t.shown_at, DELAY + 1000);
    CHECK(!ScreenTitle_moves(&t, DELAY * 3));
}

TEST(a_deferred_change_of_a_still_title_is_at_once) {
    ScreenTitle t;
    ScreenTitle_reset(&t, true);
    ScreenTitle_measure(&t, AREA, GAP, FADE, 0);
    ScreenTitle_set(&t, "/Music", 100, 0);

    CHECK(ScreenTitle_set(&t, "/Music/B", 300, 5000));
    CHECK_EQ_INT(strcmp(t.seg[0].text, "/Music/B"), 0);
    CHECK_EQ_INT(t.count, 1);

    // A wide title in its start delay does not move yet, thus it changes at once.
    CHECK(ScreenTitle_set(&t, "/Music/C", 300, 5000 + DELAY - 1));
    CHECK_EQ_INT(strcmp(t.seg[0].text, "/Music/C"), 0);
    CHECK_EQ_INT(t.count, 1);
}

// The window is 100 px in: it shows the text from 100 to its end at 300, thus
// the text keeps its room with no cut, and the new one follows the gap.
TEST(a_deferred_change_at_the_end_of_the_text_keeps_it_whole) {
    ScreenTitle t = moving_title(true, 0);

    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(100)));
    CHECK_EQ_INT(t.count, 2);
    CHECK_EQ_INT(strcmp(t.seg[0].text, "A long title"), 0);
    CHECK(!t.seg[0].cut);
    CHECK_EQ_INT(t.seg[0].len, 300);
    CHECK_EQ_INT(strcmp(t.seg[1].text, "/Music/B"), 0);
    CHECK(!t.seg[1].cut);

    // The movement goes on from where it was
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(100)), 100);
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(200)), 200);

    // The head leaves the window after its room and the gap, and the new text
    // keeps the movement with no delay of its own
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(300 + GAP - 1)), 300 + GAP - 1);
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(300 + GAP)), 0);
    CHECK_EQ_INT(t.count, 1);
    CHECK_EQ_INT(strcmp(t.seg[0].text, "/Music/B"), 0);
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(300 + GAP + 50)), 50);
}

// The window is 50 px in: its edge is at 250, inside the text.
TEST(a_deferred_change_cuts_inside_the_text) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(50)));
    CHECK(t.seg[0].cut);
    CHECK_EQ_INT(t.seg[0].len, 250 + FADE);
}

// A text that fits scrolls in and comes to rest when it reaches the window.
TEST(a_deferred_text_that_fits_scrolls_in_and_stops) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/Music", 100, at_px(100)));
    CHECK_EQ_INT(t.count, 2);

    int arrival = 300 + GAP;
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(arrival - 1)), arrival - 1);
    CHECK(ScreenTitle_moves(&t, at_px(arrival - 1)));

    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(arrival)), 0);
    CHECK_EQ_INT(strcmp(t.seg[0].text, "/Music"), 0);
    CHECK(!ScreenTitle_moves(&t, at_px(arrival)));
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(arrival) + 5000), 0);
    // At rest and off the surface: a surface frame draws it
    CHECK(ScreenTitle_needsFrame(&t, at_px(arrival) + 5000, false));
}

// A text that the window has not reached can be replaced, and nothing shows it.
TEST(a_text_not_yet_in_the_window_is_replaced) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(100)));
    CHECK(!ScreenTitle_set(&t, "/Music/C", 300, at_px(120)));
    CHECK_EQ_INT(t.count, 2);
    CHECK_EQ_INT(strcmp(t.seg[1].text, "/Music/C"), 0);
    // The head keeps its room from the first change: the window did not move past it
    CHECK_EQ_INT(t.seg[0].len, 300);
}

// A text that the window shows already stays, and the new one follows it.
TEST(a_visible_text_stays_and_the_new_one_follows) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(100)));
    // The window is at 200: it shows the head from 200 to 300, the gap and
    // 70 px of /Music/B
    uint32_t later = at_px(200);
    CHECK_EQ_INT(ScreenTitle_offset(&t, later), 200);
    CHECK(!ScreenTitle_set(&t, "/Music/C", 300, later));
    CHECK_EQ_INT(t.count, 3);
    CHECK_EQ_INT(strcmp(t.seg[1].text, "/Music/B"), 0);
    CHECK(t.seg[1].cut);
    CHECK_EQ_INT(t.seg[1].len, 70 + FADE);
    CHECK_EQ_INT(strcmp(t.seg[2].text, "/Music/C"), 0);
}

// A short text that the window shows takes the room up to the edge, thus the
// new one comes in after it and not over it.
TEST(a_short_visible_text_is_padded_to_the_edge) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/", 20, at_px(100)));
    // The window is at 300: the head ended, then the gap, then "/" and 150 px
    // of nothing
    uint32_t later = at_px(300);
    CHECK_EQ_INT(ScreenTitle_offset(&t, later), 300);
    CHECK(!ScreenTitle_set(&t, "/Music/C", 300, later));
    CHECK_EQ_INT(t.count, 3);
    CHECK(!t.seg[1].cut);
    CHECK_EQ_INT(t.seg[1].len, 500 - (300 + GAP));
    CHECK_EQ_INT(strcmp(t.seg[2].text, "/Music/C"), 0);
}

// The window is 200 px in, thus its edge at 400 is in the next copy of the
// text that loops: that copy is on the screen, and it goes on the tape cut.
TEST(a_change_while_the_loop_shows_the_next_copy) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(200)));
    CHECK_EQ_INT(t.count, 3);
    CHECK_EQ_INT(strcmp(t.seg[0].text, "A long title"), 0);
    CHECK(!t.seg[0].cut);
    CHECK_EQ_INT(t.seg[0].len, 300);
    CHECK_EQ_INT(strcmp(t.seg[1].text, "A long title"), 0);
    CHECK(t.seg[1].cut);
    CHECK_EQ_INT(t.seg[1].len, 70 + FADE);
    CHECK_EQ_INT(strcmp(t.seg[2].text, "/Music/B"), 0);
}

// The window edge in the gap after a text: the text keeps its room.
TEST(a_change_while_the_edge_is_in_the_gap) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(110)));
    CHECK_EQ_INT(t.count, 2);
    CHECK(!t.seg[0].cut);
    CHECK_EQ_INT(t.seg[0].len, 300);
}

// A text that looped is at an offset inside its loop, thus a change scrolls in
// from there and does not drop the head.
TEST(a_change_after_a_loop_scrolls_in) {
    ScreenTitle t = moving_title(true, 0);
    // 380 px moved: one loop of 330 and 50 into the next
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(380)), 50);
    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(380)));
    CHECK_EQ_INT(t.count, 2);
    CHECK_EQ_INT(strcmp(t.seg[0].text, "A long title"), 0);
    CHECK_EQ_INT(ScreenTitle_offset(&t, at_px(381)), 51);
    CHECK_EQ_INT(strcmp(t.seg[0].text, "A long title"), 0);
}

// A setting pill narrows the area under a title at rest: the marquee starts
// from the start, after its delay, and not from where an old clock says.
TEST(a_narrower_area_starts_the_marquee_from_the_start) {
    ScreenTitle t;
    ScreenTitle_reset(&t, false);
    ScreenTitle_measure(&t, 400, GAP, FADE, 0);
    ScreenTitle_set(&t, "A long title", 300, 0);
    CHECK(!ScreenTitle_moves(&t, 100000));

    ScreenTitle_measure(&t, AREA, GAP, FADE, 100000);
    CHECK(!ScreenTitle_moves(&t, 100000 + DELAY - 1));
    CHECK_EQ_INT(ScreenTitle_offset(&t, 100000 + DELAY), 0);
    CHECK_EQ_INT(ScreenTitle_offset(&t, 100000 + DELAY + 1000), SPEED);
}

// A full tape of texts that the window shows: the oldest one after the head
// leaves, and the text under the edge stays.
TEST(a_full_tape_keeps_the_text_under_the_edge) {
    ScreenTitle t;
    ScreenTitle_reset(&t, true);
    ScreenTitle_measure(&t, 600, GAP, FADE, 0);
    ScreenTitle_set(&t, "A very long title", 1200, 0);

    // A short text every 81 px: each one is in the window before the next comes
    char name[SCREEN_TITLE_SEGMENTS + 2][8];
    for (int i = 0; i < SCREEN_TITLE_SEGMENTS + 1; i++) {
        snprintf(name[i], sizeof(name[i]), "/%d", i);
        CHECK(!ScreenTitle_set(&t, name[i], 10, at_px(10 + 81 * i)));
    }
    CHECK_EQ_INT(t.count, SCREEN_TITLE_SEGMENTS);
    CHECK_EQ_INT(strcmp(t.seg[0].text, "A very long title"), 0);
    // The text under the edge stayed, and the newest follows it
    CHECK_EQ_INT(strcmp(t.seg[SCREEN_TITLE_SEGMENTS - 2].text, name[SCREEN_TITLE_SEGMENTS - 1]), 0);
    CHECK_EQ_INT(strcmp(t.seg[SCREEN_TITLE_SEGMENTS - 1].text, name[SCREEN_TITLE_SEGMENTS]), 0);
}

// The window ends at the start of the next text: that text is not in the
// window, thus a change replaces it and nothing of it shows.
TEST(a_text_that_starts_at_the_edge_is_not_in_the_window) {
    ScreenTitle t = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&t, "/Music/B", 300, at_px(100)));
    // At 130 the window is [130, 330): /Music/B starts at 330
    CHECK(!ScreenTitle_set(&t, "/Music/C", 300, at_px(130)));
    CHECK_EQ_INT(t.count, 2);
    CHECK_EQ_INT(strcmp(t.seg[1].text, "/Music/C"), 0);
    CHECK(!t.seg[0].cut);
    CHECK_EQ_INT(t.seg[0].len, 300);

    // The same edge on a text that loops: the next copy starts at the edge
    ScreenTitle u = moving_title(true, 0);
    CHECK(!ScreenTitle_set(&u, "/Music/B", 300, at_px(130)));
    CHECK_EQ_INT(u.count, 2);
    CHECK_EQ_INT(strcmp(u.seg[1].text, "/Music/B"), 0);
}

TEST(a_surface_frame_is_due_when_the_surface_disagrees) {
    ScreenTitle t = moving_title(false, 0);
    // At rest: the surface must show it
    CHECK(ScreenTitle_needsFrame(&t, DELAY - 1, false));
    CHECK(!ScreenTitle_needsFrame(&t, DELAY - 1, true));
    // Moving: the surface must not
    CHECK(ScreenTitle_needsFrame(&t, DELAY, true));
    CHECK(!ScreenTitle_needsFrame(&t, DELAY, false));
}

TEST(a_title_holds_a_path_of_the_browser) {
    char path[SCREEN_TITLE_MAX + 100];
    memset(path, 'a', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    ScreenTitle t;
    ScreenTitle_reset(&t, true);
    CHECK(ScreenTitle_set(&t, path, 3000, 0));
    CHECK_EQ_SZ(strlen(t.seg[0].text), SCREEN_TITLE_MAX - 1);
}

int main(void) {
    RUN(a_title_that_fits_stays_still);
    RUN(the_same_text_is_no_change);
    RUN(a_wide_title_waits_for_the_start_delay);
    RUN(the_offset_follows_the_clock);
    RUN(one_text_loops_after_its_gap);
    RUN(a_wider_area_stops_the_title);
    RUN(a_new_text_replaces_the_title_at_once_without_defer);
    RUN(a_deferred_change_of_a_still_title_is_at_once);
    RUN(a_deferred_change_at_the_end_of_the_text_keeps_it_whole);
    RUN(a_deferred_change_cuts_inside_the_text);
    RUN(a_deferred_text_that_fits_scrolls_in_and_stops);
    RUN(a_text_not_yet_in_the_window_is_replaced);
    RUN(a_visible_text_stays_and_the_new_one_follows);
    RUN(a_short_visible_text_is_padded_to_the_edge);
    RUN(a_change_while_the_loop_shows_the_next_copy);
    RUN(a_change_while_the_edge_is_in_the_gap);
    RUN(a_change_after_a_loop_scrolls_in);
    RUN(a_narrower_area_starts_the_marquee_from_the_start);
    RUN(a_full_tape_keeps_the_text_under_the_edge);
    RUN(a_text_that_starts_at_the_edge_is_not_in_the_window);
    RUN(a_surface_frame_is_due_when_the_surface_disagrees);
    RUN(a_title_holds_a_path_of_the_browser);
    return test_summary();
}
