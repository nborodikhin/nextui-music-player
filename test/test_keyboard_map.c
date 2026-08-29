#include <string.h>

#include "test.h"
#include "keyboard_map.h"

// Every character of every pairs string, so a malformed table shows up here
// rather than as a key typing the wrong letter
TEST(pairs_are_complete) {
    for (int map = 0; map < KEYBOARD_MAP_COUNT; map++) {
        for (int row = 0; row < KEYBOARD_ROWS; row++) {
            int length = KeyboardMap_rowLength(row);
            for (int col = 0; col < length; col++) {
                const KeyboardKey* key = &KeyboardMap_row(row)[col];
                const char* pairs = KeyboardMap_pairs(KeyboardMap_get(map), key);
                if (!pairs) continue;

                int count = KeyboardMap_variantCount(pairs);
                CHECK(count > 0);

                // The pairs are exactly the characters the string holds: an odd
                // one left over means a variant lost its shifted half
                int characters = 0;
                for (const char* c = pairs; *c; c++) {
                    if (((unsigned char)*c & 0xC0) != 0x80) characters++;
                }
                CHECK(characters == count * 2);

                // Every pair the count promises can be read back
                for (int index = 0; index < count; index++) {
                    char lower[KEYBOARD_TEXT_SIZE];
                    char upper[KEYBOARD_TEXT_SIZE];
                    KeyboardMap_variant(pairs, index, false, lower);
                    KeyboardMap_variant(pairs, index, true, upper);
                    CHECK(lower[0] != '\0');
                    CHECK(upper[0] != '\0');
                }
            }
        }
    }
}

// A key's primary is its first pair, and the alternates follow it
TEST(variant_indexing) {
    const char* pairs = "aAàÀáÁ";

    CHECK(KeyboardMap_variantCount(pairs) == 3);

    char out[KEYBOARD_TEXT_SIZE];
    KeyboardMap_variant(pairs, 0, false, out);
    CHECK(strcmp(out, "a") == 0);
    KeyboardMap_variant(pairs, 0, true, out);
    CHECK(strcmp(out, "A") == 0);
    KeyboardMap_variant(pairs, 2, false, out);
    CHECK(strcmp(out, "á") == 0);

    // Past the end is empty, not the last pair over again
    KeyboardMap_variant(pairs, 3, false, out);
    CHECK(out[0] == '\0');
    KeyboardMap_variant(NULL, 0, false, out);
    CHECK(out[0] == '\0');
}

// A trailing character with no shifted half is not a pair
TEST(odd_pairs_are_not_counted) {
    CHECK(KeyboardMap_variantCount("aAb") == 1);
    CHECK(KeyboardMap_variantCount("") == 0);
    CHECK(KeyboardMap_variantCount(NULL) == 0);
}

// The Cyrillic map leaves digits and most punctuation to the Latin one
TEST(map_inherits_from_base) {
    const KeyboardMap* cyrillic = KeyboardMap_get(1);

    const KeyboardKey* backtick = NULL;
    const KeyboardKey* letter = NULL;
    for (int col = 0; col < KeyboardMap_rowLength(0); col++) {
        const KeyboardKey* key = &KeyboardMap_row(0)[col];
        if (key->ansi == '`') backtick = key;
    }
    for (int col = 0; col < KeyboardMap_rowLength(2); col++) {
        const KeyboardKey* key = &KeyboardMap_row(2)[col];
        if (key->ansi == 'a') letter = key;
    }
    CHECK(backtick != NULL);
    CHECK(letter != NULL);

    char out[KEYBOARD_TEXT_SIZE];
    KeyboardMap_variant(KeyboardMap_pairs(cyrillic, backtick), 0, false, out);
    CHECK(strcmp(out, "`") == 0);

    KeyboardMap_variant(KeyboardMap_pairs(cyrillic, letter), 0, false, out);
    CHECK(strcmp(out, "ф") == 0);
}

// Only the keys that type anything have pairs
TEST(special_keys_have_no_pairs) {
    const KeyboardMap* latin = KeyboardMap_get(0);

    for (int col = 0; col < KeyboardMap_rowLength(4); col++) {
        const KeyboardKey* key = &KeyboardMap_row(4)[col];
        if (!KeyboardMap_isSpecial(key)) continue;
        CHECK(KeyboardMap_pairs(latin, key) == NULL);
    }
}

TEST(apostrophe_popup_keeps_the_same_characters) {
    const KeyboardMap* latin = KeyboardMap_get(KEYBOARD_MAP_LATIN);
    const KeyboardKey* apostrophe = NULL;
    for (int col = 0; col < KeyboardMap_rowLength(2); col++) {
        const KeyboardKey* key = &KeyboardMap_row(2)[col];
        if (key->ansi == '\'') apostrophe = key;
    }
    CHECK(apostrophe != NULL);

    const char* lower[] = {"'", "\"", "`", "‘", "’", "„", "“", "”", "«", "»", "ʻ", "ʼ"};
    const char* upper[] = {"\"", "'", "`", "‘", "’", "„", "“", "”", "«", "»", "ʻ", "ʼ"};
    const char* pairs = KeyboardMap_pairs(latin, apostrophe);
    CHECK(KeyboardMap_variantCount(pairs) == 12);

    for (int index = 0; index < 12; index++) {
        char out[KEYBOARD_TEXT_SIZE];
        KeyboardMap_variant(pairs, index, false, out);
        CHECK(strcmp(out, lower[index]) == 0);
        KeyboardMap_variant(pairs, index, true, out);
        CHECK(strcmp(out, upper[index]) == 0);
    }
}

// A font that draws nothing beyond ASCII keeps every primary and no alternate
static bool ascii_only(void* context, const char* c) {
    (void)context;
    return ((unsigned char)*c & 0x80) == 0;
}

TEST(filter_keeps_primaries) {
    KeyboardMap_prepare(ascii_only, NULL);

    const KeyboardMap* latin = KeyboardMap_get(0);
    const KeyboardMap* cyrillic = KeyboardMap_get(1);

    const KeyboardKey* a = NULL;
    for (int col = 0; col < KeyboardMap_rowLength(2); col++) {
        const KeyboardKey* key = &KeyboardMap_row(2)[col];
        if (key->ansi == 'a') a = key;
    }
    CHECK(a != NULL);

    // "a" loses its accents, "ф" is a primary and survives as tofu
    CHECK(KeyboardMap_variantCount(KeyboardMap_pairs(latin, a)) == 1);
    CHECK(KeyboardMap_variantCount(KeyboardMap_pairs(cyrillic, a)) == 1);

    char out[KEYBOARD_TEXT_SIZE];
    KeyboardMap_variant(KeyboardMap_pairs(cyrillic, a), 0, false, out);
    CHECK(strcmp(out, "ф") == 0);
}

// Rows: 0 digits, 1 tab, 2 home, 3 shift, 4 space
static void check_step(int row, int col, int step, int want_row, int want_col) {
    int got_row = -1;
    int got_col = -1;
    KeyboardMap_step(row, col, step, &got_row, &got_col);
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

// A move that has nowhere to go leaves the cursor where it was
TEST(step_stays_put_off_grid) {
    check_step(0, KEYBOARD_COLS, 1, 0, KEYBOARD_COLS);
    check_step(-1, 0, 1, -1, 0);    // the text field is not a row here
    check_step(4, 1, 1, 4, 1);      // the gap beside the space bar
}

int main(void) {
    RUN(pairs_are_complete);
    RUN(variant_indexing);
    RUN(odd_pairs_are_not_counted);
    RUN(map_inherits_from_base);
    RUN(special_keys_have_no_pairs);
    RUN(apostrophe_popup_keeps_the_same_characters);
    RUN(filter_keeps_primaries);
    RUN(step_down);
    RUN(step_up);
    RUN(step_stays_put_off_grid);
    return test_summary();
}
