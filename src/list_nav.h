#ifndef __LIST_NAV_H__
#define __LIST_NAV_H__

#include <stdbool.h>

// Cursor state for a scrollable list or menu: the shared half of the
// Up/Down/Left/Right handling every list screen needs.

// Navigation intents for one frame, decoded from the pad by ListNavPad_read().
// More than one bit may be set; ListNav_step() applies the first match in the
// order UP, DOWN, LEFT, RIGHT.
typedef enum {
    LIST_NAV_NONE      = 0,
    LIST_NAV_UP        = 1 << 0,
    LIST_NAV_DOWN      = 1 << 1,
    LIST_NAV_LEFT      = 1 << 2,
    LIST_NAV_RIGHT     = 1 << 3
} ListNavInput;

// Decode this frame's platform D-pad state into navigation intents.
ListNavInput ListNavPad_read(void);

// Invariant: selected is -1 exactly when count is 0, and a valid row otherwise.
// A caller may index its items with `selected` after checking `count > 0`.
typedef struct {
    int  selected;           // -1 only when the list is empty
    int  scroll;             // index of the first visible row; unused when external_scroll
    int  count;              // caller refreshes before every step
    int  items_per_page;     // caller refreshes before every step; <1 is treated as 1

    // If set, ListNav call won't update `scroll`.
    // Useful for the cases where the caller owns the window and tracks the offset in pixels itself.
    bool external_scroll;
} ListNav;

typedef struct {
    bool selection_changed;
    bool scroll_changed;
    bool moved;              // selection_changed || scroll_changed
} ListNavChange;

// Apply one frame of input.
//
// Also settles the window when ListNav owns one (anything not external_scroll),
// so `scroll` is consistent with `selected` on return and the returned change
// covers the window.
ListNavChange ListNav_step(ListNav* nav, ListNavInput in);

// Re-establish invariants after list size changed.
// Settles the window on the same terms as ListNav_step().
ListNavChange ListNav_reconcile(ListNav* nav, int new_count);

// Call to update list position on list mutation.
//
// Standard behavior:
//   index <  selected   selected item is kept on the same screen row
//   index == selected   the item below slid into this slot
//   index >  selected   below the cursor: nothing moves
ListNavChange ListNav_onItemRemoved(ListNav* nav, int index);

// `n` consecutive items removed starting at `index` - a background worker that
// completes items off the front of a queue passes index 0.
ListNavChange ListNav_onItemsRemoved(ListNav* nav, int index, int n);

// Cursor to the first row, window to the top. An external_scroll caller's
// window is left alone - reset that alongside.
void ListNav_scrollToTop(ListNav* nav);

// Pull `scroll` so `selected` is on screen, for call sites tracking their own.
void ListNav_adjustScroll(int selected, int* scroll, int items_per_page);

// Page math, for call sites that do track selection themselves.
// Non-positive items_per_page is treated as 1.
bool ListNav_pageUp(int* selected, int* scroll, int total_count, int items_per_page);
bool ListNav_pageDown(int* selected, int* scroll, int total_count, int items_per_page);

#endif
