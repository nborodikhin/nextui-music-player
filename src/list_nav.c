#include "list_nav.h"

// Pure integer logic - no platform headers here, on purpose. See list_nav.h.

static int clamp_ipp(int items_per_page) {
    return (items_per_page < 1) ? 1 : items_per_page;
}

// Pages the window, preserving selected position on the screen.
// When there is not enough content to fill the window, the selection is moved instead.
static bool page_move(int* selected, int* scroll, int total_count, int items_per_page, int dir) {
    int ipp        = clamp_ipp(items_per_page);
    int max_scroll = (total_count > ipp) ? total_count - ipp : 0;

    int old_selected = *selected;
    int old_scroll   = *scroll;

    *selected += dir * ipp;
    if (*selected >= total_count) *selected = total_count - 1;
    if (*selected < 0)            *selected = 0;   // also catches total_count 0

    *scroll += dir * ipp;
    if (*scroll < 0)          *scroll = 0;
    if (*scroll > max_scroll) *scroll = max_scroll;

    return (*scroll != old_scroll || *selected != old_selected);
}

bool ListNav_pageUp(int* selected, int* scroll, int total_count, int items_per_page) {
    return page_move(selected, scroll, total_count, items_per_page, -1);
}

bool ListNav_pageDown(int* selected, int* scroll, int total_count, int items_per_page) {
    return page_move(selected, scroll, total_count, items_per_page, 1);
}

static bool owns_window(const ListNav* nav) {
    return !nav->external_scroll;
}

static void clamp_scroll(ListNav* nav) {
    int ipp        = clamp_ipp(nav->items_per_page);
    int max_scroll = (nav->count > ipp) ? nav->count - ipp : 0;
    if (nav->scroll > max_scroll) nav->scroll = max_scroll;
    if (nav->scroll < 0) nav->scroll = 0;
}

// Pull an owned window to the cursor, then off the end of the list so a shrink
// cannot leave it showing one row on an otherwise empty screen.
static void settle_window(ListNav* nav) {
    if (!owns_window(nav)) return;

    ListNav_adjustScroll(nav->selected, &nav->scroll, nav->items_per_page);
    clamp_scroll(nav);
}

static ListNavChange change_from(const ListNav* nav, int old_selected, int old_scroll) {
    ListNavChange ch;
    ch.selection_changed = (nav->selected != old_selected);
    ch.scroll_changed    = (nav->scroll   != old_scroll);
    ch.moved             = ch.selection_changed || ch.scroll_changed;
    return ch;
}

ListNavChange ListNav_step(ListNav* nav, ListNavInput in) {
    int old_selected = nav->selected;
    int old_scroll   = nav->scroll;

    if (nav->count <= 0) {
        // An empty list can never move the cursor or the window.
        return change_from(nav, old_selected, old_scroll);
    }

    // The empty-list sentinel never survives into a non-empty list.
    if (nav->selected < 0) {
        nav->selected = 0;
    }
    if (nav->selected >= nav->count) {
        nav->selected = nav->count - 1;
    }

    int ipp = clamp_ipp(nav->items_per_page);

    // First match wins.
    if (in & LIST_NAV_UP) {
        nav->selected = (nav->selected > 0) ? nav->selected - 1 : nav->count - 1;
    }
    else if (in & LIST_NAV_DOWN) {
        nav->selected = (nav->selected < nav->count - 1) ? nav->selected + 1 : 0;
    }
    else if (in & (LIST_NAV_LEFT | LIST_NAV_RIGHT)) {
        int dir = (in & LIST_NAV_LEFT) ? -1 : 1;

        if (nav->external_scroll) {
            int throwaway = 0;   // the caller's window is not ours to move
            page_move(&nav->selected, &throwaway, nav->count, ipp, dir);
        } else {
            page_move(&nav->selected, &nav->scroll, nav->count, ipp, dir);
        }
    }

    settle_window(nav);

    return change_from(nav, old_selected, old_scroll);
}

ListNavChange ListNav_reconcile(ListNav* nav, int new_count) {
    int old_selected = nav->selected;
    int old_scroll   = nav->scroll;

    nav->count = new_count;

    if (new_count <= 0) {
        nav->selected = -1;
        if (!nav->external_scroll) nav->scroll = 0;
        return change_from(nav, old_selected, old_scroll);
    }

    if (nav->selected >= new_count) {
        nav->selected = new_count - 1;
    }
    if (nav->selected < 0) {
        nav->selected = 0;   // the list just stopped being empty
    }

    settle_window(nav);

    return change_from(nav, old_selected, old_scroll);
}

ListNavChange ListNav_onItemsRemoved(ListNav* nav, int index, int n) {
    int old_selected = nav->selected;
    int old_scroll   = nav->scroll;

    for (int i = 0; i < n; i++) {
        ListNav_onItemRemoved(nav, index);
    }
    return change_from(nav, old_selected, old_scroll);
}

ListNavChange ListNav_onItemRemoved(ListNav* nav, int index) {
    int old_selected = nav->selected;
    int old_scroll   = nav->scroll;

    if (index < 0 || index >= nav->count) {
        return change_from(nav, old_selected, old_scroll);   // not a real removal
    }

    nav->count--;

    if (nav->count <= 0) {
        nav->count    = 0;
        nav->selected = -1;
        if (!nav->external_scroll) nav->scroll = 0;
        return change_from(nav, old_selected, old_scroll);
    }

    if (index < nav->selected) {
        // Everything below shifted up: move the window with the cursor to keep
        // the selected item on its screen row. The clamps below give that up
        // when the window cannot go far enough.
        nav->selected--;
        if (!nav->external_scroll) nav->scroll--;
    }

    if (nav->selected >= nav->count) {
        nav->selected = nav->count - 1;   // was the last row
    }

    if (!nav->external_scroll) clamp_scroll(nav);

    return change_from(nav, old_selected, old_scroll);
}

void ListNav_adjustScroll(int selected, int* scroll, int items_per_page) {
    int ipp = clamp_ipp(items_per_page);

    if (selected < *scroll) {
        *scroll = selected;
    }
    if (selected >= *scroll + ipp) {
        *scroll = selected - ipp + 1;
    }
    if (*scroll < 0) {
        *scroll = 0;
    }
}

void ListNav_scrollToTop(ListNav* nav) {
    nav->selected = (nav->count > 0) ? 0 : -1;
    if (!nav->external_scroll) nav->scroll = 0;
}

