#include <string.h>

#include "test.h"
#include "keyboard_map.h"

// A font that draws everything, so the layouts come through whole
static bool supports_all(void* context, const char* c) {
    (void)context;
    (void)c;
    return true;
}

static const KeyboardGeometry* geometry(void) {
    return KeyboardMap_get()->geometry;
}

// The layouts, in the order the map hands them out
enum { LATIN, CYRILLIC };

static const KeyboardLayout* layout(int index) {
    return KeyboardMap_get()->layouts[index];
}

// Keys in a row, which the NULL at its end marks
static int row_length(int row) {
    int length = 0;
    while (geometry()->keys[row][length]) length++;
    return length;
}

// Find a key by the ANSI character its slot stands for
static const Key* key_at(int row, char ansi) {
    for (const Key* const* key = geometry()->keys[row]; *key; key++) {
        if ((*key)->ansi == ansi) return *key;
    }
    return NULL;
}

// Every character a key claims can be read back, on every layout: a pairs
// string with an odd character in the table would lose one here rather than in
// a key that types the wrong letter
TEST(every_char_reads_back) {
    int keys_seen = 0;

    for (int index = 0; index < KeyboardMap_get()->layout_count; index++) {
        for (int row = 0; row < geometry()->rows; row++) {
            for (const Key* const* key = geometry()->keys[row]; *key; key++) {
                int count = KeyboardMap_charCount(layout(index), *key);
                if (count == 0) continue;
                keys_seen++;

                for (int c = 0; c < count; c++) {
                    CHECK(KeyboardMap_char(layout(index), *key, false, c)[0] != '\0');
                    CHECK(KeyboardMap_char(layout(index), *key, true, c)[0] != '\0');
                }
            }
        }
    }

    // Both layouts, every row: a walk that found nothing would pass silently
    CHECK(keys_seen > 60);
}

// A key's primary is its first pair, and the alternates follow it
TEST(char_indexing) {
    const Key* a = key_at(2, 'a');
    CHECK(a != NULL);

    const KeyboardLayout* latin = layout(LATIN);
    int count = KeyboardMap_charCount(latin, a);
    CHECK(count > 3);

    CHECK(strcmp(KeyboardMap_char(latin, a, false, 0), "a") == 0);
    CHECK(strcmp(KeyboardMap_char(latin, a, true, 0), "A") == 0);
    CHECK(strcmp(KeyboardMap_char(latin, a, false, 1), "á") == 0);

    // Past either end is empty, not the nearest pair over again
    CHECK(KeyboardMap_char(latin, a, false, count)[0] == '\0');
    CHECK(KeyboardMap_char(latin, a, false, -1)[0] == '\0');
    CHECK(KeyboardMap_char(NULL, a, false, 0)[0] == '\0');
    CHECK(KeyboardMap_char(latin, NULL, false, 0)[0] == '\0');
    CHECK(KeyboardMap_charCount(latin, NULL) == 0);
    CHECK(KeyboardMap_charCount(NULL, a) == 0);
}

// The Cyrillic layout leaves digits and most punctuation to the Latin one
TEST(layout_inherits_from_base) {
    const KeyboardLayout* cyrillic = layout(CYRILLIC);

    const Key* backtick = key_at(0, '`');
    const Key* letter = key_at(2, 'a');
    CHECK(backtick != NULL);
    CHECK(letter != NULL);

    CHECK(strcmp(KeyboardMap_char(cyrillic, backtick, false, 0), "`") == 0);
    CHECK(strcmp(KeyboardMap_char(cyrillic, letter, false, 0), "ф") == 0);
}

// Only the keys that type anything have characters
TEST(special_keys_type_nothing) {
    const KeyboardLayout* latin = layout(LATIN);

    for (const Key* const* key = geometry()->keys[4]; *key; key++) {
        if ((*key)->action == KEY_TEXT) continue;
        CHECK(KeyboardMap_charCount(latin, *key) == 0);
    }
}

TEST(apostrophe_popup_keeps_the_same_characters) {
    const KeyboardLayout* latin = layout(LATIN);
    const Key* apostrophe = key_at(2, '\'');
    CHECK(apostrophe != NULL);

    const char* lower[] = {"'", "\"", "`", "‘", "’", "„",
                           "“", "”", "«", "»", "ʻ", "ʼ"};
    const char* upper[] = {"\"", "'", "`", "‘", "’", "„",
                           "“", "”", "«", "»", "ʻ", "ʼ"};
    CHECK(KeyboardMap_charCount(latin, apostrophe) == 12);

    for (int index = 0; index < 12; index++) {
        CHECK(strcmp(KeyboardMap_char(latin, apostrophe, false, index), lower[index]) == 0);
        CHECK(strcmp(KeyboardMap_char(latin, apostrophe, true, index), upper[index]) == 0);
    }
}

// Every row spans the geometry's width, so the rows line up however many keys
// they hold. The last key stops short of it: what is left over is the gap at
// the end of the row.
TEST(rows_fit_the_geometry_width) {
    for (int row = 0; row < geometry()->rows; row++) {
        float left = 0.0f;
        for (const Key* const* key = geometry()->keys[row]; *key; key++) {
            CHECK((*key)->left == left);
            CHECK((*key)->row == row);
            left += (*key)->width;
        }
        CHECK(left <= geometry()->width);
    }
}

// A font that draws nothing beyond ASCII keeps every primary and no alternate
static bool ascii_only(void* context, const char* c) {
    (void)context;
    return ((unsigned char)*c & 0x80) == 0;
}

TEST(prepare_keeps_primaries) {
    KeyboardMap_prepare(ascii_only, NULL);

    const Key* a = key_at(2, 'a');
    CHECK(a != NULL);

    const KeyboardLayout* latin = layout(LATIN);
    const KeyboardLayout* cyrillic = layout(CYRILLIC);

    // "a" loses its accents, "ф" is a primary and survives as tofu
    CHECK(KeyboardMap_charCount(latin, a) == 1);
    CHECK(KeyboardMap_charCount(cyrillic, a) == 1);
    CHECK(strcmp(KeyboardMap_char(cyrillic, a, false, 0), "ф") == 0);
}

// Rows: 0 digits, 1 tab, 2 home, 3 shift, 4 space.
// Follows a key's col_up/col_down the way keyboard.c does: the rows wrap, and a
// column the target row does not reach leaves the cursor where it was.
static void check_step(int row, int col, int step, int want_row, int want_col) {
    const Key* key = geometry()->keys[row][col];
    int landing = (step > 0) ? key->col_down : key->col_up;

    int got_row = row;
    int got_col = col;
    if (landing != KEY_NO_COLUMN) {
        int rows = geometry()->rows;
        int target = (row + step + rows) % rows;
        if (landing < row_length(target)) {
            got_row = target;
            got_col = landing;
        }
    }

    CHECK(got_row == want_row);
    CHECK(got_col == want_col);
}

// Down the columns: the rows have different key counts, so the ends bunch up
TEST(step_down) {
    check_step(0, 0, 1, 1, 0);      // ` -> tab
    check_step(0, 1, 1, 1, 1);      // 1 -> q
    check_step(0, 10, 1, 1, 10);    // 0 -> p
    check_step(0, 12, 1, 1, 12);    // = -> ]
    check_step(0, 13, 1, 1, 13);    // backspace -> backslash

    check_step(1, 0, 1, 2, 0);      // tab -> caps
    check_step(1, 10, 1, 2, 10);    // p -> ;
    check_step(1, 11, 1, 2, 11);    // [ -> '
    check_step(1, 12, 1, 2, 11);    // ] -> '
    check_step(1, 13, 1, 2, 12);    // backslash -> enter

    check_step(2, 0, 1, 3, 0);      // caps -> left shift
    check_step(2, 1, 1, 3, 1);      // a -> z
    check_step(2, 5, 1, 3, 5);      // g -> b
    check_step(2, 6, 1, 3, 5);      // h -> b
    check_step(2, 11, 1, 3, 10);    // ' -> /
    check_step(2, 12, 1, 3, 11);    // enter -> right shift

    // Every letter drops into the space bar; only the shifts flank it
    check_step(3, 0, 1, 4, 0);      // left shift -> lang
    check_step(3, 5, 1, 4, 2);      // b -> space
    check_step(3, 10, 1, 4, 2);     // / -> space
    check_step(3, 11, 1, 4, 4);     // right shift -> cancel

    check_step(4, 0, 1, 0, 0);      // lang -> `
    check_step(4, 2, 1, 0, 6);      // space -> 6
    check_step(4, 4, 1, 0, 13);     // cancel -> backspace
}

// Up is the mirror of down, except where two keys shared one target
TEST(step_up) {
    check_step(4, 0, -1, 3, 0);     // lang -> left shift
    check_step(4, 2, -1, 3, 5);     // space -> b
    check_step(4, 4, -1, 3, 11);    // cancel -> right shift

    check_step(3, 1, -1, 2, 1);     // z -> a
    check_step(3, 5, -1, 2, 5);     // b -> g, not h
    check_step(3, 11, -1, 2, 12);   // right shift -> enter

    check_step(2, 11, -1, 1, 11);   // ' -> [
    check_step(2, 12, -1, 1, 13);   // enter -> backslash

    check_step(1, 1, -1, 0, 1);     // q -> 1

    check_step(0, 0, -1, 4, 0);     // ` -> lang
    check_step(0, 6, -1, 4, 2);     // 6 -> space
    check_step(0, 5, -1, 4, 2);     // 5 -> space
    check_step(0, 13, -1, 4, 4);    // backspace -> cancel
}

// A spacer holds a place in its row and nothing else. What a spacer itself
// leads to is never read - the cursor cannot be on one - but no key may lead
// *to* one, or a step up or down would land the cursor where it cannot sit.
TEST(nothing_leads_to_a_spacer) {
    int rows = geometry()->rows;
    int spacers = 0;

    for (int row = 0; row < rows; row++) {
        int above = (row - 1 + rows) % rows;
        int below = (row + 1) % rows;

        for (const Key* const* key = geometry()->keys[row]; *key; key++) {
            if ((*key)->action == KEY_SPACER) spacers++;

            int up = (*key)->col_up;
            if (up != KEY_NO_COLUMN && up < row_length(above)) {
                CHECK(geometry()->keys[above][up]->action != KEY_SPACER);
            }

            int down = (*key)->col_down;
            if (down != KEY_NO_COLUMN && down < row_length(below)) {
                CHECK(geometry()->keys[below][down]->action != KEY_SPACER);
            }
        }
    }

    CHECK(spacers == 2);
}

int main(void) {
    // The keyboard is what _prepare() builds; nothing can be read before it runs
    KeyboardMap_prepare(supports_all, NULL);

    RUN(every_char_reads_back);
    RUN(char_indexing);
    RUN(layout_inherits_from_base);
    RUN(special_keys_type_nothing);
    RUN(apostrophe_popup_keeps_the_same_characters);
    RUN(rows_fit_the_geometry_width);
    RUN(step_down);
    RUN(step_up);
    RUN(nothing_leads_to_a_spacer);

    // Rebuilds the layouts with a font that has no glyph past ASCII
    RUN(prepare_keeps_primaries);
    return test_summary();
}
