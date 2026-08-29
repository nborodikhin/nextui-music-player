#include <string.h>

#include "test.h"
#include "keyboard_map.h"

// A font that draws everything, so the maps come through whole
static bool supports_all(void* context, const char* c) {
    (void)context;
    (void)c;
    return true;
}

// Every alternate a key claims can be read back, on both maps: a pairs string
// with an odd character in the table would lose one here rather than in a key
// that types the wrong letter
TEST(every_variant_reads_back) {
    int keys_seen = 0;

    for (int map = 0; map < KEYBOARD_MAP_COUNT; map++) {
        for (int row = 0; row < KeyboardMap_rowCount(); row++) {
            int length = KeyboardMap_rowLength(row);
            for (int col = 0; col < length; col++) {
                const KeyboardKey* key = &KeyboardMap_row(row)[col];
                const KeyMapping* mapping = KeyboardMap_key(KeyboardMap_get(map), key);
                if (!mapping) continue;

                int count = KeyboardMap_variantCount(mapping);
                CHECK(count > 0);
                keys_seen++;

                for (int index = 0; index < count; index++) {
                    CHECK(KeyboardMap_variant(mapping, index, false)[0] != '\0');
                    CHECK(KeyboardMap_variant(mapping, index, true)[0] != '\0');
                }
            }
        }
    }

    // Both maps, every row: a walk that found nothing would pass silently
    CHECK(keys_seen > 60);
}

// Find a key by the ANSI character its slot stands for
static const KeyboardKey* key_at(int row, char ansi) {
    for (int col = 0; col < KeyboardMap_rowLength(row); col++) {
        const KeyboardKey* key = &KeyboardMap_row(row)[col];
        if (key->ansi == ansi) return key;
    }
    return NULL;
}

// A key's primary is its first pair, and the alternates follow it
TEST(variant_indexing) {
    const KeyboardKey* a = key_at(2, 'a');
    CHECK(a != NULL);

    const KeyMapping* mapping = KeyboardMap_key(KeyboardMap_get(KEYBOARD_MAP_LATIN), a);
    CHECK(KeyboardMap_variantCount(mapping) > 3);

    CHECK(strcmp(KeyboardMap_variant(mapping, 0, false), "a") == 0);
    CHECK(strcmp(KeyboardMap_variant(mapping, 0, true), "A") == 0);
    CHECK(strcmp(KeyboardMap_variant(mapping, 1, false), "\u00e1") == 0);

    // Past either end is empty, not the nearest pair over again
    int count = KeyboardMap_variantCount(mapping);
    CHECK(KeyboardMap_variant(mapping, count, false)[0] == '\0');
    CHECK(KeyboardMap_variant(mapping, -1, false)[0] == '\0');
    CHECK(KeyboardMap_variant(NULL, 0, false)[0] == '\0');
    CHECK(KeyboardMap_variantCount(NULL) == 0);
}

// The Cyrillic map leaves digits and most punctuation to the Latin one
TEST(map_inherits_from_base) {
    const KeyboardMap* cyrillic = KeyboardMap_get(KEYBOARD_MAP_CYRILLIC);

    const KeyboardKey* backtick = key_at(0, '`');
    const KeyboardKey* letter = key_at(2, 'a');
    CHECK(backtick != NULL);
    CHECK(letter != NULL);

    CHECK(strcmp(KeyboardMap_variant(KeyboardMap_key(cyrillic, backtick), 0, false),
                 "`") == 0);
    CHECK(strcmp(KeyboardMap_variant(KeyboardMap_key(cyrillic, letter), 0, false),
                 "\u0444") == 0);
}

// Only the keys that type anything have characters
TEST(special_keys_type_nothing) {
    const KeyboardMap* latin = KeyboardMap_get(KEYBOARD_MAP_LATIN);

    for (int col = 0; col < KeyboardMap_rowLength(4); col++) {
        const KeyboardKey* key = &KeyboardMap_row(4)[col];
        if (!KeyboardMap_isSpecial(key)) continue;
        CHECK(KeyboardMap_key(latin, key) == NULL);
    }
}

TEST(apostrophe_popup_keeps_the_same_characters) {
    const KeyboardMap* latin = KeyboardMap_get(KEYBOARD_MAP_LATIN);
    const KeyboardKey* apostrophe = key_at(2, '\'');
    CHECK(apostrophe != NULL);

    const char* lower[] = {"'", "\"", "`", "\u2018", "\u2019", "\u201e",
                           "\u201c", "\u201d", "\u00ab", "\u00bb", "\u02bb", "\u02bc"};
    const char* upper[] = {"\"", "'", "`", "\u2018", "\u2019", "\u201e",
                           "\u201c", "\u201d", "\u00ab", "\u00bb", "\u02bb", "\u02bc"};
    const KeyMapping* mapping = KeyboardMap_key(latin, apostrophe);
    CHECK(KeyboardMap_variantCount(mapping) == 12);

    for (int index = 0; index < 12; index++) {
        CHECK(strcmp(KeyboardMap_variant(mapping, index, false), lower[index]) == 0);
        CHECK(strcmp(KeyboardMap_variant(mapping, index, true), upper[index]) == 0);
    }
}

// A font that draws nothing beyond ASCII keeps every primary and no alternate
static bool ascii_only(void* context, const char* c) {
    (void)context;
    return ((unsigned char)*c & 0x80) == 0;
}

TEST(prepare_keeps_primaries) {
    KeyboardMap_prepare(ascii_only, NULL);

    const KeyboardKey* a = key_at(2, 'a');
    CHECK(a != NULL);

    const KeyMapping* latin = KeyboardMap_key(KeyboardMap_get(KEYBOARD_MAP_LATIN), a);
    const KeyMapping* cyrillic = KeyboardMap_key(KeyboardMap_get(KEYBOARD_MAP_CYRILLIC), a);

    // "a" loses its accents, "\u0444" is a primary and survives as tofu
    CHECK(KeyboardMap_variantCount(latin) == 1);
    CHECK(KeyboardMap_variantCount(cyrillic) == 1);
    CHECK(strcmp(KeyboardMap_variant(cyrillic, 0, false), "\u0444") == 0);
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
    check_step(0, 99, 1, 0, 99);    // a column past the end of any row
    check_step(-1, 0, 1, -1, 0);    // the text field is not a row here
    check_step(4, 1, 1, 4, 1);      // the gap beside the space bar
}

int main(void) {
    // The maps are what _prepare() builds; nothing can be read before it runs
    KeyboardMap_prepare(supports_all, NULL);

    RUN(every_variant_reads_back);
    RUN(variant_indexing);
    RUN(map_inherits_from_base);
    RUN(special_keys_type_nothing);
    RUN(apostrophe_popup_keeps_the_same_characters);
    RUN(step_down);
    RUN(step_up);
    RUN(step_stays_put_off_grid);

    // Rebuilds the maps with a font that has no glyph past ASCII
    RUN(prepare_keeps_primaries);
    return test_summary();
}
