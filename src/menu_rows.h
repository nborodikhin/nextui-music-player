#ifndef __MENU_ROWS_H__
#define __MENU_ROWS_H__

#include <stdbool.h>

// The main menu's row map: which rows are on screen right now, and what each
// one means. The single derivation of it - the input loop and the renderer both
// read rows through here, so a conditional row is added in one place.
//
// Pure integer logic, no platform includes.

// A menu row, or the outcome of running the menu. Returned by MenuModule_run().
typedef enum {
    MENU_NONE        = -1,  // no such row; never an outcome

    // Resume and Now Playing are the same row in two states: at most one is on
    // screen, and only the first row is ever either of them.
    MENU_RESUME      = 0,
    MENU_NOW_PLAYING = 1,

    MENU_LIBRARY     = 2,
    MENU_RADIO       = 3,
    MENU_PODCAST     = 4,
    MENU_SETTINGS    = 5,

    MENU_QUIT        = 100  // exit the application
} MenuSelection;

// Upper bound on visible rows. 5 today (first item + four fixed); the extra
// slot is headroom for a conditional row without resizing every caller.
#define MENU_ROWS_MAX 6

typedef struct {
    MenuSelection playing_item;             // MENU_RESUME, MENU_NOW_PLAYING or MENU_NONE
    int           count;                    // number of visible rows
    MenuSelection selection[MENU_ROWS_MAX];
} MenuRows;

// Build the row map.
// `playing_item` is the state of the playback row: MENU_RESUME,
// MENU_NOW_PLAYING, or MENU_NONE to leave it off the menu.
MenuRows MenuRows_build(MenuSelection playing_item);

// Row index -> selection. MENU_NONE for an out-of-range row.
MenuSelection MenuRows_selectionAt(const MenuRows* rows, int row);

// Selection -> row index, or -1 when that item is not on screen.
// Note: performs exact match (Resume selection would not resolve to Now Player item)
int MenuRows_rowOf(const MenuRows* rows, MenuSelection selection);

// True for the row that represents a playback item (Now Playing/Resume).
bool MenuRows_isPlayItem(MenuSelection selection);

// Row of the play/resume item, or -1 when neither state is on screen.
int MenuRows_getPlayingItemRow(const MenuRows* rows);

// True when both maps hold the same rows in the same order. The caller repaints
// when they differ - a row that appears, disappears or changes state moves every
// row below it without moving the cursor.
bool MenuRows_equal(const MenuRows* a, const MenuRows* b);

#endif
