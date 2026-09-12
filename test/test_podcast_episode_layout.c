// Tests for src/podcast_episode_layout.c - where the summary and the rows of the
// episode list are, and how far the content scrolls.
//
// Pins the height of the content, the position of the first row after a
// summary of any height, the scroll that keeps the selected row visible, the
// clamp at each end, the empty feed, and the wrap of the cursor from the first
// row to the last through ListNav.

#include "test.h"
#include "podcast_episode_layout.h"
#include "list_nav.h"

// The layouts of the device screens at 640 by 480 and at 1024 by 768.
#define GAP 10

TEST(rows_start_after_the_summary_and_its_gap) {
    PodcastEpisodeLayout a = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 5, 0, 0);
    CHECK_EQ_INT(a.first_row_y, 110);
    CHECK_EQ_INT(a.content_h, 110 + 5 * 90);

    // A summary of another height moves the first row by that height alone
    PodcastEpisodeLayout b = PodcastEpisodeLayout_compute(40, GAP, 400, 90, 5, 0, 0);
    CHECK_EQ_INT(b.first_row_y, 50);
    CHECK_EQ_INT(b.content_h, 50 + 5 * 90);
}

TEST(a_short_summary_leaves_room_for_more_than_one_row) {
    // 640 by 480 at scale 2: a viewport of about 300 and rows of 90
    PodcastEpisodeLayout l = PodcastEpisodeLayout_compute(60, GAP, 300, 90, 10, 0, 0);
    CHECK_EQ_INT(l.scroll, 0);
    int visible = (300 - l.first_row_y) / 90;
    CHECK(visible >= 2);
}

TEST(the_first_row_shows_the_summary) {
    PodcastEpisodeLayout l = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 0, 500);
    CHECK_EQ_INT(l.scroll, 0);
}

TEST(a_tall_summary_scrolls_to_show_the_first_row) {
    PodcastEpisodeLayout l = PodcastEpisodeLayout_compute(500, GAP, 400, 90, 3, 0, 0);
    // The bottom of row 0 is at the bottom of the viewport
    CHECK_EQ_INT(l.scroll, 510 + 90 - 400);
}

TEST(the_scroll_follows_the_selected_row) {
    // Down past the viewport: the row sits at the bottom
    PodcastEpisodeLayout down = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 4, 0);
    CHECK_EQ_INT(down.scroll, 110 + 5 * 90 - 400);

    // The scroll stays where a row is visible already
    PodcastEpisodeLayout same = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 3, down.scroll);
    CHECK_EQ_INT(same.scroll, down.scroll);

    // Up past the top: the row sits at the top
    PodcastEpisodeLayout far = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 8, 0);
    CHECK_EQ_INT(far.scroll, 110 + 9 * 90 - 400);
    PodcastEpisodeLayout up = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 3, far.scroll);
    CHECK_EQ_INT(up.scroll, 110 + 3 * 90);
}

TEST(the_scroll_stops_at_the_ends_of_the_content) {
    PodcastEpisodeLayout last = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 9, 0);
    CHECK_EQ_INT(last.scroll, last.content_h - 400);

    PodcastEpisodeLayout over = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 5, 100000);
    CHECK(over.scroll <= over.content_h - 400);

    PodcastEpisodeLayout under = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, 5, -100);
    CHECK(under.scroll >= 0);

    // Content shorter than the viewport does not scroll
    PodcastEpisodeLayout fits = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 2, 1, 50);
    CHECK_EQ_INT(fits.scroll, 0);
}

TEST(an_empty_feed_is_its_summary) {
    PodcastEpisodeLayout l = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 0, -1, 30);
    CHECK_EQ_INT(l.content_h, 100);
    CHECK_EQ_INT(l.scroll, 0);
    CHECK_EQ_INT(l.first_row_y, 110);

    // A summary taller than the viewport still scrolls no further than its end
    PodcastEpisodeLayout tall = PodcastEpisodeLayout_compute(500, GAP, 400, 90, 0, -1, 1000);
    CHECK_EQ_INT(tall.scroll, 100);
}

TEST(up_on_the_first_row_wraps_to_the_last) {
    ListNav nav = {
        .selected        = 0,
        .scroll          = 0,
        .count           = 10,
        .items_per_page  = 3,
        .external_scroll = true,
    };
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_UP);
    CHECK(ch.selection_changed);
    CHECK_EQ_INT(nav.selected, 9);

    PodcastEpisodeLayout l = PodcastEpisodeLayout_compute(100, GAP, 400, 90, 10, nav.selected, 0);
    CHECK_EQ_INT(l.scroll, l.content_h - 400);
}

int main(void) {
    RUN(rows_start_after_the_summary_and_its_gap);
    RUN(a_short_summary_leaves_room_for_more_than_one_row);
    RUN(the_first_row_shows_the_summary);
    RUN(a_tall_summary_scrolls_to_show_the_first_row);
    RUN(the_scroll_follows_the_selected_row);
    RUN(the_scroll_stops_at_the_ends_of_the_content);
    RUN(an_empty_feed_is_its_summary);
    RUN(up_on_the_first_row_wraps_to_the_last);
    return test_summary();
}
