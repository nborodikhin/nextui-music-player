// Tests for src/menu_rows.c - the main menu's row map.
//
// Pins which row each MENU_* item sits on in every first-item mode, and the
// reverse lookup the renderer uses to find a row.

#include "test.h"
#include "menu_rows.h"

// State A - no first item: 4 rows, Library through Settings.
TEST(no_playing_item) {
    MenuRows rows = MenuRows_build(MENU_NONE);
    CHECK_EQ_INT(rows.count, 4);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 0), MENU_LIBRARY);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 1), MENU_RADIO);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 2), MENU_PODCAST);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 3), MENU_SETTINGS);
    CHECK(rows.playing_item == MENU_NONE);
}

// States B and C - first item present: 5 rows, everything shifts down one.
TEST(with_playing_item_resume) {
    MenuRows rows = MenuRows_build(MENU_RESUME);
    CHECK_EQ_INT(rows.count, 5);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 0), MENU_RESUME);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 1), MENU_LIBRARY);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 2), MENU_RADIO);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 3), MENU_PODCAST);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 4), MENU_SETTINGS);
    CHECK_EQ_INT(rows.playing_item, MENU_RESUME);
}

// Now Playing takes the same row as Resume, with its own selection value.
TEST(now_playing_shares_the_resume_slot) {
    MenuRows rows = MenuRows_build(MENU_NOW_PLAYING);
    CHECK_EQ_INT(rows.count, 5);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 0), MENU_NOW_PLAYING);
    CHECK(MENU_NOW_PLAYING != MENU_RESUME);
    CHECK_EQ_INT(rows.playing_item, MENU_NOW_PLAYING);
}

// The direction ui_main.c needs for the update badge: where is Settings today?
TEST(row_of_settings_tracks_the_playing_item) {
    MenuRows none = MenuRows_build(MENU_NONE);
    CHECK_EQ_INT(MenuRows_rowOf(&none, MENU_SETTINGS), 3);

    MenuRows resume = MenuRows_build(MENU_RESUME);
    CHECK_EQ_INT(MenuRows_rowOf(&resume, MENU_SETTINGS), 4);

    MenuRows playing = MenuRows_build(MENU_NOW_PLAYING);
    CHECK_EQ_INT(MenuRows_rowOf(&playing, MENU_SETTINGS), 4);
}

// Round-trip: every row maps to a selection that maps back to the same row.
TEST(round_trip_all_modes) {
    MenuSelection modes[] = { MENU_NONE, MENU_RESUME, MENU_NOW_PLAYING };
    for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        MenuRows rows = MenuRows_build(modes[m]);
        for (int row = 0; row < rows.count; row++) {
            MenuSelection sel = MenuRows_selectionAt(&rows, row);
            CHECK_EQ_INT(MenuRows_rowOf(&rows, sel), row);
        }
    }
}

// Out-of-range rows read as "nothing selected" rather than indexing garbage.
// This is the state the menu is in for one frame when a 5-row menu collapses to
// 4 with the cursor parked on the last row.
//
// It must NOT be MENU_QUIT: main() exits the application on that value, so an
// off-the-end cursor plus an A-press would quit the app.
TEST(out_of_range_row_is_none_not_quit) {
    MenuRows rows = MenuRows_build(MENU_NONE);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, 4), MENU_NONE);
    CHECK_EQ_INT(MenuRows_selectionAt(&rows, -1), MENU_NONE);
    CHECK(MENU_NONE != MENU_QUIT);
}

// An item that is not on screen has no row.
TEST(absent_selection_has_no_row) {
    MenuRows rows = MenuRows_build(MENU_NONE);
    CHECK_EQ_INT(MenuRows_rowOf(&rows, MENU_RESUME), -1);
}

// The cursor's identity must survive the first row disappearing mid-session:
// parked on Library at row 1, the audio ends and row 1 becomes Radio. Mapping
// the remembered MENU_* item through the new map is what keeps it on Library.
TEST(identity_survives_first_row_disappearing) {
    MenuRows playing = MenuRows_build(MENU_NOW_PLAYING);
    int row = 1;
    MenuSelection item = MenuRows_selectionAt(&playing, row);
    CHECK_EQ_INT(item, MENU_LIBRARY);

    MenuRows none = MenuRows_build(MENU_NONE);
    CHECK_EQ_INT(MenuRows_selectionAt(&none, row), MENU_RADIO);   // same row, other item
    CHECK_EQ_INT(MenuRows_rowOf(&none, item), 0);                 // the fix
}

// ...and the first row appearing, which shifts everything the other way.
TEST(identity_survives_first_row_appearing) {
    MenuRows none = MenuRows_build(MENU_NONE);
    MenuSelection item = MenuRows_selectionAt(&none, 2);
    CHECK_EQ_INT(item, MENU_PODCAST);

    MenuRows resume = MenuRows_build(MENU_RESUME);
    CHECK_EQ_INT(MenuRows_selectionAt(&resume, 2), MENU_RADIO);   // same row, other item
    CHECK_EQ_INT(MenuRows_rowOf(&resume, item), 3);               // the fix
}

// An item that vanished has no row, so the caller must fall back positionally.
TEST(identity_absent_after_resume_cleared) {
    MenuRows none = MenuRows_build(MENU_NONE);
    CHECK_EQ_INT(MenuRows_rowOf(&none, MENU_RESUME), -1);
}

// The row map never overflows the caller-visible bound.
TEST(count_within_bounds) {
    MenuSelection modes[] = { MENU_NONE, MENU_RESUME, MENU_NOW_PLAYING };
    for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        MenuRows rows = MenuRows_build(modes[m]);
        CHECK(rows.count > 0 && rows.count <= MENU_ROWS_MAX);
    }
}

// The play item is either state; nothing else is.
TEST(is_play_item) {
    CHECK(MenuRows_isPlayItem(MENU_RESUME));
    CHECK(MenuRows_isPlayItem(MENU_NOW_PLAYING));
    CHECK(!MenuRows_isPlayItem(MENU_LIBRARY));
    CHECK(!MenuRows_isPlayItem(MENU_SETTINGS));
    CHECK(!MenuRows_isPlayItem(MENU_NONE));
}

// Its row is found whichever state is on screen, where an exact lookup for the
// other state finds nothing.
TEST(playing_item_row_ignores_which_state) {
    MenuRows playing = MenuRows_build(MENU_NOW_PLAYING);
    CHECK_EQ_INT(MenuRows_getPlayingItemRow(&playing), 0);
    CHECK_EQ_INT(MenuRows_rowOf(&playing, MENU_RESUME), -1);

    MenuRows resume = MenuRows_build(MENU_RESUME);
    CHECK_EQ_INT(MenuRows_getPlayingItemRow(&resume), 0);
    CHECK_EQ_INT(MenuRows_rowOf(&resume, MENU_NOW_PLAYING), -1);
}

// No play row on screen at all.
TEST(playing_item_row_absent) {
    MenuRows none = MenuRows_build(MENU_NONE);
    CHECK_EQ_INT(MenuRows_getPlayingItemRow(&none), -1);
}

// A map only equals itself.
TEST(equal_same_mode) {
    MenuRows a = MenuRows_build(MENU_NOW_PLAYING);
    MenuRows b = MenuRows_build(MENU_NOW_PLAYING);
    CHECK(MenuRows_equal(&a, &b));
}

// The reported bug: playback ends while the cursor sits on row 0. The cursor
// does not move and reconcile does not clamp, so the map compare is the only
// thing that can report the change.
TEST(equal_detects_playback_row_disappearing) {
    MenuRows playing = MenuRows_build(MENU_NOW_PLAYING);
    MenuRows stopped = MenuRows_build(MENU_NONE);
    CHECK(!MenuRows_equal(&playing, &stopped));
}

// Same row count, different state - "Now Playing" becoming "Resume" relabels
// row 0 without moving anything.
TEST(equal_detects_state_swap_at_same_count) {
    MenuRows now = MenuRows_build(MENU_NOW_PLAYING);
    MenuRows res = MenuRows_build(MENU_RESUME);
    CHECK_EQ_INT(now.count, res.count);
    CHECK(!MenuRows_equal(&now, &res));
}

// A freshly-entered menu must always repaint.
TEST(equal_rejects_the_sentinel) {
    MenuRows sentinel = { .playing_item = MENU_NONE, .count = -1 };
    MenuRows rows = MenuRows_build(MENU_NONE);
    CHECK(!MenuRows_equal(&rows, &sentinel));
}

int main(void) {
    printf("test_menu_rows\n");
    RUN(no_playing_item);
    RUN(with_playing_item_resume);
    RUN(now_playing_shares_the_resume_slot);
    RUN(row_of_settings_tracks_the_playing_item);
    RUN(round_trip_all_modes);
    RUN(out_of_range_row_is_none_not_quit);
    RUN(absent_selection_has_no_row);
    RUN(identity_survives_first_row_disappearing);
    RUN(identity_survives_first_row_appearing);
    RUN(identity_absent_after_resume_cleared);
    RUN(is_play_item);
    RUN(playing_item_row_ignores_which_state);
    RUN(playing_item_row_absent);
    RUN(count_within_bounds);
    RUN(equal_same_mode);
    RUN(equal_detects_playback_row_disappearing);
    RUN(equal_detects_state_swap_at_same_count);
    RUN(equal_rejects_the_sentinel);
    return test_summary();
}
