// Tests for src/list_nav.c.
//
// Characterization: these pin the cursor behavior every list screen relies on,
// so a change that moves any of them is a change users can see.

#include "test.h"
#include "list_nav.h"

static ListNav make(int selected, int scroll, int count, int ipp) {
    ListNav nav;
    nav.selected           = selected;
    nav.scroll             = scroll;
    nav.count              = count;
    nav.items_per_page     = ipp;
    nav.external_scroll    = false;
    return nav;
}

// A list whose caller owns the window (podcast Home / Episodes keep `scroll` in
// pixels). ListNav must never write `scroll` for these.
static ListNav make_external(int selected, int count, int ipp) {
    ListNav nav = make(selected, 0, count, ipp);
    nav.external_scroll = true;
    nav.scroll = 4321;            // a pixel offset ListNav has no business touching
    return nav;
}

// 1. Wrap - all 19 sites agree on this.
TEST(wrap_up_down) {
    ListNav nav = make(0, 0, 3, 9);
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_UP);
    CHECK_EQ_INT(nav.selected, 2);
    CHECK(ch.moved);
    CHECK(ch.selection_changed);
    CHECK(!ch.scroll_changed);

    nav = make(2, 0, 3, 9);
    ch = ListNav_step(&nav, LIST_NAV_DOWN);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK(ch.moved);
}

// Single-item list: both directions wrap onto the same item, so nothing moved.
TEST(wrap_single_item_reports_no_move) {
    ListNav nav = make(0, 0, 1, 9);
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_UP);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK(!ch.moved);

    ch = ListNav_step(&nav, LIST_NAV_DOWN);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK(!ch.moved);
}

// 2. Empty list - the invariant behind every `&& station_count > 0` guard.
TEST(empty_list_never_moves) {
    ListNavInput all[] = { LIST_NAV_UP, LIST_NAV_DOWN, LIST_NAV_LEFT, LIST_NAV_RIGHT };
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        ListNav nav = make(0, 0, 0, 9);
        ListNavChange ch = ListNav_step(&nav, all[i]);
        CHECK_EQ_INT(nav.selected, 0);
        CHECK_EQ_INT(nav.scroll, 0);
        CHECK(!ch.moved);
    }
}

// 3. Paging: repeat "scroll one row keeping the screen row, or move the cursor
//    when the window cannot scroll" once per visible row.

// A list shorter than a page has no window to move, so a page press reaches the
// ends - what a menu wants.
TEST(page_short_list_reaches_the_ends) {
    ListNav nav = make(0, 0, 3, 9);
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.selected, 2);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK(ch.selection_changed);
    CHECK(!ch.scroll_changed);

    ch = ListNav_step(&nav, LIST_NAV_LEFT);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK(ch.moved);
}

// Same, from a cursor that starts mid-list.
TEST(page_reaches_the_ends_when_window_pinned) {
    ListNav nav = make(1, 0, 3, 9);
    ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK_EQ_INT(nav.selected, 2);

    ListNav_step(&nav, LIST_NAV_LEFT);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK_EQ_INT(nav.selected, 0);
}

// A long list has a full page of window to move, so the cursor keeps its screen
// row and the content moves under it.
TEST(page_keeps_the_screen_row_on_a_long_list) {
    ListNav nav = make(2, 0, 20, 5);          // row 2 of 5
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.scroll, 5);
    CHECK_EQ_INT(nav.selected, 7);
    CHECK_EQ_INT(nav.selected - nav.scroll, 2);
    CHECK(ch.selection_changed);
    CHECK(ch.scroll_changed);

    ch = ListNav_step(&nav, LIST_NAV_LEFT);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK_EQ_INT(nav.selected, 2);
    CHECK(ch.moved);
}

// A list only slightly taller than a page does both: the window moves as far as
// it can, and the cursor spends the remaining steps walking to the last item.
// 6 rows on a 5-row screen is the settings list on a Brick.
TEST(page_list_slightly_taller_than_a_page) {
    ListNav nav = make(0, 0, 6, 5);
    ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.selected, 5);            // the last item...
    CHECK_EQ_INT(nav.scroll, 1);              // ...and a window that shows it

    ListNav_step(&nav, LIST_NAV_LEFT);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK_EQ_INT(nav.scroll, 0);
}

TEST(page_moves_cursor_and_window) {
    ListNav nav = make(0, 0, 12, 5);
    ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.selected, 5);
    CHECK_EQ_INT(nav.scroll, 5);
}

// Near the end the window runs out first, and the remaining steps carry the
// cursor down through the last page rather than stalling it.
TEST(page_near_the_end_advances_the_cursor) {
    ListNav nav = make(0, 0, 7, 5);
    ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.scroll, 2);              // max_scroll = 7 - 5
    CHECK_EQ_INT(nav.selected, 5);
}

// Non-positive items_per_page is treated as 1.
TEST(page_zero_items_per_page_treated_as_one) {
    ListNav nav = make(0, 0, 5, 0);
    ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.scroll, 1);
    CHECK_EQ_INT(nav.selected, 1);
}

// 6. selected is -1 exactly when count is 0. A non-empty list normalizes the
//    sentinel away rather than navigating from it, so Up lands on the first row
//    and not on the last.
TEST(sentinel_never_survives_a_non_empty_list) {
    ListNav nav = make(-1, 0, 10, 5);
    ListNav_step(&nav, LIST_NAV_NONE);
    CHECK_EQ_INT(nav.selected, 0);

    // Normalization happens before the move, so the cursor navigates from row 0
    // by the ordinary rules - Up wraps to the last row, as it does anywhere.
    nav = make(-1, 0, 10, 5);
    ListNav_step(&nav, LIST_NAV_DOWN);
    CHECK_EQ_INT(nav.selected, 1);
}

// A list that fills up hands the cursor a real row.
TEST(sentinel_cleared_when_items_appear) {
    ListNav nav = make(-1, 0, 0, 5);
    ListNav_reconcile(&nav, 4);
    CHECK_EQ_INT(nav.selected, 0);
}

// 7. Reconcile - replaces the five hand-written clamps (D8).
TEST(reconcile_shrinking_count) {
    ListNav nav = make(7, 5, 10, 5);
    ListNavChange ch = ListNav_reconcile(&nav, 3);
    CHECK_EQ_INT(nav.count, 3);
    CHECK_EQ_INT(nav.selected, 2);
    CHECK(ch.selection_changed);
}

// reconcile clamps the cursor and NOTHING else. Two renderers keep `scroll` in
// pixels; a row-count clamp here destroyed their offset every frame, which
// looked like the cursor being stuck at the bottom while the list scrolled.
TEST(reconcile_never_touches_scroll) {
    ListNav nav = make_external(7, 10, 5);
    ListNavChange ch = ListNav_reconcile(&nav, 3);
    CHECK_EQ_INT(nav.scroll, 4321);
    CHECK(!ch.scroll_changed);

    ListNav empty = make_external(7, 10, 5);
    ListNav_reconcile(&empty, 0);
    CHECK_EQ_INT(empty.scroll, 4321);
}

TEST(reconcile_to_empty) {
    ListNav nav = make(7, 5, 10, 5);
    ListNav_reconcile(&nav, 0);
    CHECK_EQ_INT(nav.selected, -1);
    CHECK_EQ_INT(nav.scroll, 0);
}

// 7b. ListNav_onItemRemoved: the caller reports WHERE, ListNav derives what it
//     means. Three cases, one rule each.

// Above the cursor: everything below shifts up, so the window moves with the
// cursor and the selected item holds its screen row.
TEST(removed_above_holds_the_screen_row) {
    // 12 items, 3 visible, cursor on the last item at the bottom row.
    ListNav nav = make(11, 9, 12, 3);
    ListNav_onItemRemoved(&nav, 0);
    CHECK_EQ_INT(nav.count, 11);
    CHECK_EQ_INT(nav.selected, 10);
    CHECK_EQ_INT(nav.scroll, 8);
    CHECK_EQ_INT(nav.selected - nav.scroll, 2);   // still the bottom row

    // Several completing between frames is just the call applied repeatedly.
    for (int i = 0; i < 3; i++) ListNav_onItemRemoved(&nav, 0);
    CHECK_EQ_INT(nav.count, 8);
    CHECK_EQ_INT(nav.selected, 7);
    CHECK_EQ_INT(nav.selected - nav.scroll, 2);
}

TEST(removed_above_holds_row_mid_list) {
    ListNav nav = make(6, 5, 12, 3);   // row 1 of 3
    ListNav_onItemRemoved(&nav, 2);
    ListNav_onItemRemoved(&nav, 2);
    CHECK_EQ_INT(nav.selected, 4);
    CHECK_EQ_INT(nav.scroll, 3);
    CHECK_EQ_INT(nav.selected - nav.scroll, 1);
}

// At the cursor: the item below slides into the slot, so the cursor does not
// move at all - it now points at a different item, which is what the user sees.
TEST(removed_at_cursor_keeps_the_slot) {
    ListNav nav = make(4, 3, 10, 3);
    ListNavChange ch = ListNav_onItemRemoved(&nav, 4);
    CHECK_EQ_INT(nav.count, 9);
    CHECK_EQ_INT(nav.selected, 4);
    CHECK_EQ_INT(nav.scroll, 3);
    CHECK(!ch.moved);
}

// ...unless it was the last row, where there is nothing to slide up.
TEST(removed_at_cursor_on_last_row_steps_back) {
    ListNav nav = make(9, 7, 10, 3);
    ListNav_onItemRemoved(&nav, 9);
    CHECK_EQ_INT(nav.selected, 8);
    CHECK_EQ_INT(nav.scroll, 6);   // max_scroll = 9 - 3
}

// Below the cursor: nothing the user is looking at moved.
TEST(removed_below_cursor_moves_nothing) {
    ListNav nav = make(2, 1, 10, 3);
    ListNavChange ch = ListNav_onItemRemoved(&nav, 7);
    CHECK_EQ_INT(nav.count, 9);
    CHECK_EQ_INT(nav.selected, 2);
    CHECK_EQ_INT(nav.scroll, 1);
    CHECK(!ch.moved);
}

// The row is given up only when it cannot be honoured: the window is already at
// the top, so there is nothing above to scroll into.
TEST(removed_above_gives_up_the_row_when_impossible) {
    ListNav nav = make(2, 0, 5, 3);
    ListNav_onItemRemoved(&nav, 0);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK_EQ_INT(nav.selected, 1);   // walked up the screen, unavoidably
}

// The window never leaves a gap at the bottom when the list shrinks past it.
TEST(removed_never_overhangs_the_end) {
    ListNav nav = make(9, 7, 10, 3);
    for (int i = 0; i < 6; i++) ListNav_onItemRemoved(&nav, 0);
    CHECK_EQ_INT(nav.count, 4);
    CHECK_EQ_INT(nav.selected, 3);
    CHECK_EQ_INT(nav.scroll, 1);     // max_scroll = 4 - 3
}

// Draining all the way to empty.
TEST(removed_down_to_empty) {
    ListNav nav = make(2, 1, 3, 3);
    for (int i = 0; i < 3; i++) ListNav_onItemRemoved(&nav, 0);
    CHECK_EQ_INT(nav.count, 0);
    CHECK_EQ_INT(nav.selected, -1);
    CHECK_EQ_INT(nav.scroll, 0);
}

// An out-of-range index is not a removal, so nothing changes - a caller that
// reports a stale index cannot corrupt the cursor.
TEST(removed_out_of_range_is_a_noop) {
    ListNav nav = make(2, 1, 5, 3);
    ListNavChange ch = ListNav_onItemRemoved(&nav, 5);
    CHECK_EQ_INT(nav.count, 5);
    CHECK_EQ_INT(nav.selected, 2);
    CHECK(!ch.moved);

    ch = ListNav_onItemRemoved(&nav, -1);
    CHECK_EQ_INT(nav.count, 5);
    CHECK(!ch.moved);
}

TEST(reconcile_growing_count_leaves_cursor) {
    ListNav nav = make(1, 0, 3, 5);
    ListNavChange ch = ListNav_reconcile(&nav, 10);
    CHECK_EQ_INT(nav.selected, 1);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK(!ch.moved);
}

TEST(reconcile_already_valid_reports_no_move) {
    ListNav nav = make(2, 0, 10, 5);
    ListNavChange ch = ListNav_reconcile(&nav, 10);
    CHECK(!ch.moved);
}

// Cursor parked on the last row of a 5-item menu when the first item
// disappears and the menu becomes 4.
TEST(reconcile_fixes_menu_state_collapse) {
    ListNav nav = make(4, 0, 5, 9);
    ListNav_reconcile(&nav, 4);
    CHECK_EQ_INT(nav.selected, 3);
}

// 8. `moved` accuracy (D4) - a no-op page press must not force a redraw.
TEST(moved_false_on_noop_page) {
    ListNav nav = make(0, 0, 3, 9);
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_LEFT);
    CHECK(!ch.moved);
    CHECK(!ch.selection_changed);
    CHECK(!ch.scroll_changed);
}

TEST(moved_false_on_no_input) {
    ListNav nav = make(1, 0, 5, 9);
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_NONE);
    CHECK(!ch.moved);
    CHECK_EQ_INT(nav.selected, 1);
}

// 9. The window pull: cursor above the window drags it up, below drags it down,
//    inside leaves it alone.
TEST(adjust_scroll) {
    int scroll = 3;
    ListNav_adjustScroll(0, &scroll, 5);
    CHECK_EQ_INT(scroll, 0);

    scroll = 0;
    ListNav_adjustScroll(9, &scroll, 5);
    CHECK_EQ_INT(scroll, 5);

    scroll = 0;
    ListNav_adjustScroll(2, &scroll, 5);
    CHECK_EQ_INT(scroll, 0);
}

// 10. Input priority - reproduces the else-if chain when two bits are set.
TEST(input_priority_matches_else_if_chain) {
    ListNav nav = make(2, 0, 10, 5);
    ListNav_step(&nav, LIST_NAV_UP | LIST_NAV_DOWN);
    CHECK_EQ_INT(nav.selected, 1);           // UP won

    nav = make(2, 0, 10, 5);
    ListNav_step(&nav, LIST_NAV_UP | LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.selected, 1);           // UP won, window untouched
    CHECK_EQ_INT(nav.scroll, 0);

    nav = make(2, 0, 20, 5);
    ListNav_step(&nav, LIST_NAV_LEFT | LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.selected, 0);           // PAGE_UP won
}

// external_scroll is enforced, not advisory: no call may write `scroll`, not
// even a page move, which would otherwise carry the window with the cursor.
TEST(external_scroll_is_never_written) {
    ListNav nav = make_external(5, 20, 3);
    ListNav_step(&nav, LIST_NAV_RIGHT);
    CHECK_EQ_INT(nav.scroll, 4321);
    CHECK_EQ_INT(nav.selected, 8);        // cursor-only page move

    ListNav_step(&nav, LIST_NAV_LEFT);
    CHECK_EQ_INT(nav.scroll, 4321);
    CHECK_EQ_INT(nav.selected, 5);

    ListNav_step(&nav, LIST_NAV_DOWN);
    CHECK_EQ_INT(nav.scroll, 4321);
    CHECK_EQ_INT(nav.selected, 6);

    ListNav_reconcile(&nav, 8);
    CHECK_EQ_INT(nav.scroll, 4321);

    ListNav_onItemRemoved(&nav, 0);       // above the cursor: would shift a row window
    CHECK_EQ_INT(nav.scroll, 4321);
    CHECK_EQ_INT(nav.selected, 5);        // the cursor still tracks its item

}

// Draining an external-scroll list to empty must not zero the pixel offset either.
TEST(external_scroll_survives_emptying) {
    ListNav nav = make_external(1, 2, 3);
    ListNav_onItemRemoved(&nav, 0);
    ListNav_onItemRemoved(&nav, 0);
    CHECK_EQ_INT(nav.count, 0);
    CHECK_EQ_INT(nav.selected, -1);
    CHECK_EQ_INT(nav.scroll, 4321);
}

// ListNav settles the window itself for a list whose window it owns, so a wrap
// or a clamp cannot leave the cursor off screen waiting for the renderer.
TEST(owned_window_follows_the_cursor) {
    // Wrap from the first item to the last: the window has to jump to the end.
    ListNav nav = make(0, 0, 10, 5);
    ListNavChange ch = ListNav_step(&nav, LIST_NAV_UP);
    CHECK_EQ_INT(nav.selected, 9);
    CHECK_EQ_INT(nav.scroll, 5);          // 9 - 5 + 1
    CHECK(ch.scroll_changed);
    CHECK(ch.moved);

    // ...and back again.
    ch = ListNav_step(&nav, LIST_NAV_DOWN);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK_EQ_INT(nav.scroll, 0);
    CHECK(ch.scroll_changed);
}

// reconcile settles it on the same terms: a shrink that pulls the cursor back
// must bring the window with it.
TEST(owned_window_follows_reconcile) {
    ListNav nav = make(19, 15, 20, 5);
    ListNavChange ch = ListNav_reconcile(&nav, 6);
    CHECK_EQ_INT(nav.selected, 5);
    CHECK_EQ_INT(nav.scroll, 1);          // 6 - 5
    CHECK(ch.scroll_changed);
}

// A menu is not an owned window: its renderer draws every row and ignores
// `scroll`, so ListNav must not maintain one behind its back.
TEST(menu_window_is_left_alone) {
    ListNav nav = make_external(0, 12, 5);
    ListNav_step(&nav, LIST_NAV_UP);
    CHECK_EQ_INT(nav.selected, 11);
    CHECK_EQ_INT(nav.scroll, 4321);       // untouched despite being off-window

    ListNav_reconcile(&nav, 12);
    CHECK_EQ_INT(nav.scroll, 4321);
}

// Rewind to the top: cursor to the first row, window with it.
TEST(scroll_to_top) {
    ListNav nav = make(7, 5, 20, 5);
    ListNav_scrollToTop(&nav);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK_EQ_INT(nav.scroll, 0);
}

// An external_scroll caller owns its window, so the pixel offset it keeps
// elsewhere is not this function's to clear.
TEST(scroll_to_top_leaves_an_external_window) {
    ListNav nav = make_external(7, 20, 5);
    ListNav_scrollToTop(&nav);
    CHECK_EQ_INT(nav.selected, 0);
    CHECK_EQ_INT(nav.scroll, 4321);
}

// The plural form is the primitive applied n times - what a drained queue does.
TEST(items_removed_from_the_front) {
    ListNav nav = make(11, 9, 12, 3);
    ListNavChange ch = ListNav_onItemsRemoved(&nav, 0, 4);
    CHECK_EQ_INT(nav.count, 8);
    CHECK_EQ_INT(nav.selected, 7);
    CHECK_EQ_INT(nav.selected - nav.scroll, 2);   // still the bottom row
    CHECK(ch.moved);

    ListNav one = make(11, 9, 12, 3);
    for (int i = 0; i < 4; i++) ListNav_onItemRemoved(&one, 0);
    CHECK_EQ_INT(one.selected, nav.selected);
    CHECK_EQ_INT(one.scroll, nav.scroll);
}

TEST(items_removed_none_is_a_noop) {
    ListNav nav = make(4, 3, 10, 3);
    ListNavChange ch = ListNav_onItemsRemoved(&nav, 0, 0);
    CHECK_EQ_INT(nav.count, 10);
    CHECK(!ch.moved);
}

int main(void) {
    printf("test_list_nav\n");
    RUN(wrap_up_down);
    RUN(wrap_single_item_reports_no_move);
    RUN(empty_list_never_moves);
    RUN(page_short_list_reaches_the_ends);
    RUN(page_reaches_the_ends_when_window_pinned);
    RUN(page_keeps_the_screen_row_on_a_long_list);
    RUN(page_list_slightly_taller_than_a_page);
    RUN(page_moves_cursor_and_window);
    RUN(page_near_the_end_advances_the_cursor);
    RUN(page_zero_items_per_page_treated_as_one);
    RUN(sentinel_never_survives_a_non_empty_list);
    RUN(sentinel_cleared_when_items_appear);
    RUN(reconcile_shrinking_count);
    RUN(reconcile_never_touches_scroll);
    RUN(removed_above_holds_the_screen_row);
    RUN(removed_above_holds_row_mid_list);
    RUN(removed_at_cursor_keeps_the_slot);
    RUN(removed_at_cursor_on_last_row_steps_back);
    RUN(removed_below_cursor_moves_nothing);
    RUN(removed_above_gives_up_the_row_when_impossible);
    RUN(removed_never_overhangs_the_end);
    RUN(removed_down_to_empty);
    RUN(removed_out_of_range_is_a_noop);
    RUN(items_removed_from_the_front);
    RUN(items_removed_none_is_a_noop);
    RUN(reconcile_to_empty);
    RUN(reconcile_growing_count_leaves_cursor);
    RUN(reconcile_already_valid_reports_no_move);
    RUN(reconcile_fixes_menu_state_collapse);
    RUN(moved_false_on_noop_page);
    RUN(moved_false_on_no_input);
    RUN(adjust_scroll);
    RUN(input_priority_matches_else_if_chain);
    RUN(owned_window_follows_the_cursor);
    RUN(owned_window_follows_reconcile);
    RUN(menu_window_is_left_alone);
    RUN(scroll_to_top);
    RUN(scroll_to_top_leaves_an_external_window);
    RUN(external_scroll_is_never_written);
    RUN(external_scroll_survives_emptying);
    return test_summary();
}
