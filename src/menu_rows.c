#include "menu_rows.h"

bool MenuRows_isPlayItem(MenuSelection selection) {
    return selection == MENU_RESUME || selection == MENU_NOW_PLAYING;
}

MenuRows MenuRows_build(MenuSelection playing_item) {
    MenuRows rows;
    rows.playing_item = MenuRows_isPlayItem(playing_item) ? playing_item : MENU_NONE;
    rows.count = 0;

    if (rows.playing_item != MENU_NONE) {
        rows.selection[rows.count++] = rows.playing_item;
    }
    rows.selection[rows.count++] = MENU_LIBRARY;
    rows.selection[rows.count++] = MENU_RADIO;
    rows.selection[rows.count++] = MENU_PODCAST;
    rows.selection[rows.count++] = MENU_SETTINGS;

    return rows;
}

MenuSelection MenuRows_selectionAt(const MenuRows* rows, int row) {
    if (row < 0 || row >= rows->count) return MENU_NONE;
    return rows->selection[row];
}

int MenuRows_rowOf(const MenuRows* rows, MenuSelection selection) {
    for (int i = 0; i < rows->count; i++) {
        if (rows->selection[i] == selection) return i;
    }
    return -1;
}

int MenuRows_getPlayingItemRow(const MenuRows* rows) {
    if (rows->playing_item == MENU_NONE) return -1;
    return MenuRows_rowOf(rows, rows->playing_item);
}

// playing_item needs no compare of its own: when present it is selection[0],
// and when only one map has it the counts already differ.
bool MenuRows_equal(const MenuRows* a, const MenuRows* b) {
    if (a->count != b->count) return false;
    for (int i = 0; i < a->count; i++) {
        if (a->selection[i] != b->selection[i]) return false;
    }
    return true;
}
