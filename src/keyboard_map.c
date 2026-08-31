#include <stdlib.h>
#include <string.h>

#include "keyboard_map.h"
#include "utf8.h"

// Digits, tab, home, shift, space
#define KEYBOARD_ROWS 5

// Where the cursor opens: the home row, as on a typewriter
#define KEYBOARD_HOME_ROW 2

// Room for the longest row plus the NULL that ends it
#define KEYBOARD_MAX_COLS 16

// Every row is authored to this width; the keys leave the remainder over
#define KEYBOARD_ROW_WIDTH_UNITS 15.0f

// Preferred backspace label, and what to draw when the font has no arrow
#define BACKSPACE_ARROW "\u2190"
#define BACKSPACE_FALLBACK "<-"

#define COUNT_OF(array) ((int)(sizeof(array) / sizeof((array)[0])))

// A character key, one unit wide
#define K(slot) KW(slot, 1.0f)

// A character key with on explicitly defined width
#define KW(slot, key_width) {   \
    .action = KEY_TEXT,         \
    .ansi   = slot,             \
    .label  = NULL,             \
    .width  = key_width,        \
}

// A "special" key that acts and displays a label instead of a character.
#define KEY(what, slot, name, key_width) { \
    .action = what,                        \
    .ansi   = slot,                        \
    .label  = name,                        \
    .width  = key_width,                   \
}

#define K_BKSP    KEY(KEY_BACKSPACE, '\b', BACKSPACE_ARROW,  2.0f)
#define K_TAB     KEY(KEY_TAB,       '\t', "TAB",            1.5f)
#define K_CAPS    KEY(KEY_CAPS,      '\0', "CL",             1.75f)
#define K_ENTER   KEY(KEY_ENTER,     '\n', "ENTER",          2.25f)
#define K_LANG    KEY(KEY_LANG,      '\0', "LANG",           2.5f)
#define K_LSHIFT  KEY(KEY_SHIFT,     '\0', "SHIFT",          2.25f)
#define K_RSHIFT  KEY(KEY_SHIFT,     '\0', "SHIFT",          2.75f)
#define K_SPACE   KEY(KEY_SPACE,     ' ',  "SPACE",          6.25f)
#define K_CANCEL  KEY(KEY_CANCEL,    '\0', "CANCEL",         2.5f)
#define SPACER    KEY(KEY_SPACER,    '\0', NULL,             1.875f)

// Standard ANSI geometry, shared by every layout. One array per row, so the
// row's length is its own; what the keys leave short of the full width is the
// gap at the end of the row. Not const: _prepare() fills in each key's place,
// its flags and where it leads. Text keys carry the ANSI character naming their
// slot; the tables below say what each slot types.
static Key row_digits[] = {
    K('`'), K('1'), K('2'), K('3'), K('4'), K('5'), K('6'),
    K('7'), K('8'), K('9'), K('0'), K('-'), K('='), K_BKSP,
};

static Key row_tab[] = {
    K_TAB, K('q'), K('w'), K('e'), K('r'), K('t'), K('y'),
    K('u'), K('i'), K('o'), K('p'), K('['), K(']'), KW('\\', 1.5f),
};

static Key row_home[] = {
    K_CAPS, K('a'), K('s'), K('d'), K('f'), K('g'), K('h'),
    K('j'), K('k'), K('l'), K(';'), K('\''), K_ENTER,
};

static Key row_shift[] = {
    K_LSHIFT, K('z'), K('x'), K('c'), K('v'), K('b'), K('n'),
    K('m'), K(','), K('.'), K('/'), K_RSHIFT,
};

static Key row_space[] = {
    K_LANG, SPACER, K_SPACE, SPACER, K_CANCEL,
};

static Key* const authored_rows[KEYBOARD_ROWS] = {
    row_digits, row_tab, row_home, row_shift, row_space,
};

static const int authored_lengths[KEYBOARD_ROWS] = {
    COUNT_OF(row_digits), COUNT_OF(row_tab), COUNT_OF(row_home),
    COUNT_OF(row_shift),  COUNT_OF(row_space),
};

// Mapping key definition - list of characters assigned to the key.
// `pairs` is the string of UTF-8 chars, ordered in pairs (regular char, shifted char).
// The first pair is what the key types and shows; the rest are its alternates.
// A pair that that produces the same character should declare that character twice.
typedef struct {
    char        ansi;
    const char* pairs;
} SourceMapping;

// Closes a mapping table: no slot is named by the terminator
#define MAPPING_END {'\0', NULL}

// Latin language map, alternatives are ordered by how often they come up across
// the alphabets
static const SourceMapping mapping_lat[] = {
    {'`',  "`~~`"},
    {'1',  "1!!1¡¡"},
    {'2',  "2@@2"},
    {'3',  "3##3№№"},
    {'4',  "4$$4€€££¥¥¢¢₽₽₴₴₸₸"},
    {'5',  "5%%5"},
    {'6',  "6^^6°°"},
    {'7',  "7&&7§§¶¶"},
    {'8',  "8**8••××"},
    {'9',  "9((9‹‹"},
    {'0',  "0))0››"},
    {'-',  "-__-——––±±÷÷"},
    {'=',  "=++=≠≠≈≈"},

    {'q',  "qQ"},
    {'w',  "wWŵŴ"},
    {'e',  "eEéÉèÈêÊëËẽẼēĒĕĔėĖęĘəƏ"},
    {'r',  "rRřŘŕŔŗŖ"},
    {'t',  "tTťŤțȚŧŦþÞṯṮ"},
    {'y',  "yYýÝÿŸỹỸŷŶ"},
    {'u',  "uUúÚùÙûÛüÜũŨūŪŭŬűŰųŲ"},
    {'i',  "iIíÍìÌîÎïÏĩĨīĪįĮıIiİ"},
    {'o',  "oOóÓòÒôÔöÖõÕōŌøØőŐœŒǫǪȯȮ"},
    {'p',  "pP"},
    {'[',  "[{{["},
    {']',  "]}}]"},
    {'\\', "\\||\\"},

    {'a',  "aAáÁàÀâÂäÄãÃåÅāĀăĂąĄæÆ"},
    {'s',  "sSšŠśŚşŞșȘßẞŝŜ"},
    {'d',  "dDďĎđĐðÐ"},
    {'f',  "fF"},
    {'g',  "gGğĞġĠĝĜģĢǧǦǥǤ"},
    {'h',  "hHħĦĥĤȟȞ"},
    {'j',  "jJĵĴ"},
    {'k',  "kKķĶǩǨ"},
    {'l',  "lLłŁľĽĺĹļĻḻḺ"},
    {';',  ";::;"},
    {'\'', "'\"\"'``‘‘’’„„““””««»»ʻʻʼʼ"},

    {'z',  "zZžŽźŹżŻ"},
    {'x',  "xX"},
    {'c',  "cCçÇćĆčČċĊĉĈ"},
    {'v',  "vV"},
    {'b',  "bB"},
    {'n',  "nNñÑńŃňŇņŅŋŊṉṈ"},
    {'m',  "mM"},
    {',',  ",<<,‚‚"},
    {'.',  ".>>.……··"},
    {'/',  "/?\?/¿¿"},
    MAPPING_END,
};

// Cyrillic language map (overlay on top of Latin).
// Letters are ordered by languages using them, in population order:
// Russian, Ukrainian, Belarusian, Kazakh, Serbian, etc.
static const SourceMapping mapping_cyr[] = {
    {'1',  "1!!1¡¡ӏӀ"},
    {'2',  "2\"\"2„„"},
    {'3',  "3№№3"},
    {'4',  "4;;4$$€€££¥¥¢¢₽₽₴₴₸₸"},
    {'6',  "6::6"},
    {'7',  "7??7¿¿"},

    {'q',  "йЙјЈ"},
    {'w',  "цЦџЏҵҴ"},
    {'e',  "уУўЎұҰүҮӯӮӳӲӱӰ"},
    {'r',  "кКқҚќЌҡҠҟҞҝҜ"},
    {'t',  "еЕёЁєЄӗӖ"},
    {'y',  "нНңҢњЊ"},
    {'u',  "гГґҐғҒѓЃӷӶ"},
    {'i',  "шШ"},
    {'o',  "щЩ"},
    {'p',  "зЗѕЅҙҘӡӠӟӞ"},
    {'[',  "хХһҺҳҲ"},
    {']',  "ъЪ"},

    {'a',  "фФ"},
    {'s',  "ыЫӹӸ"},
    {'d',  "вВ"},
    {'f',  "аАәӘӑӐӓӒӕӔ"},
    {'g',  "пПԥԤ"},
    {'h',  "рР"},
    {'j',  "оОөӨӧӦҩҨ"},
    {'k',  "лЛљЉ"},
    {'l',  "дДђЂ"},
    {';',  "жЖӂӁӝӜ"},
    {'\'', "эЭ"},

    {'z',  "яЯ"},
    {'x',  "чЧҷҶћЋҹҸҽҼӵӴ"},
    {'c',  "сСҫҪ"},
    {'v',  "мМ"},
    {'b',  "иИіІїЇӣӢ"},
    {'n',  "тТҭҬ"},
    {'m',  "ьЬ"},
    {',',  "бБ"},
    {'.',  "юЮ"},
    {'/',  ".,,."},
    MAPPING_END,
};

// One character, in both cases (regular and shifted)
typedef struct {
    const char* plain;
    const char* shifted;
} KeyChar;

// Characters produced by a key.
struct KeyChars {
    int            count;
    const KeyChar* chars;
};

// A layout as the tables spell it.
typedef struct {
    const char*          name;
    const SourceMapping* keys;
} SourceLayout;

// List of source layouts.
// The first one must define every slot.
// The others are overlays on top of the first one and can define only keys that are different.
static const SourceLayout sources[] = {
    {
        .name = "LAT",
        .keys = mapping_lat,
    },
    {
        .name = "CYR",
        .keys = mapping_cyr,
    },
};

#define LAYOUT_COUNT COUNT_OF(sources)

static KeyboardLayout layouts[LAYOUT_COUNT];
static const KeyboardLayout* layout_pointers[LAYOUT_COUNT];

// Characters of each key
static KeyChars layout_chars[LAYOUT_COUNT][KEYBOARD_ROWS][KEYBOARD_MAX_COLS];

// Every character of every layout, extracted out of the pair strings and kept as
// its own string, and the pairs that point into them. Duplicates are tolerated.
static char (*char_pool)[UTF8_CHAR_SIZE] = NULL;
static KeyChar* char_pairs = NULL;

// The rows as they are handed out: pointers into the authored arrays, each row
// NULL-terminated and the row list too

// (Null-terminated) list of keys per row
static const Key* row_keys[KEYBOARD_ROWS][KEYBOARD_MAX_COLS];
// (Null-terminated) list of keys per row
static const Key* const* rows[KEYBOARD_ROWS + 1];

static KeyboardGeometry geometry;
static Keyboard keyboard;

// Which key of the row below each key leads to, and which of the row above.
// Above/below is not symmetrical: (G, H) -> B, ([, ]) -> ".

static const signed char below_col[KEYBOARD_ROWS][KEYBOARD_MAX_COLS] = {
    [0] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},          // digits -> tab row
    [1] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 12},          // tab row -> home row
    [2] = {0, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11},               // home row -> shift row
    [3] = {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4},                    // shift row -> space row
    [4] = {0, 0, 6, 13, 13},                                        // space row -> digits
};

static const signed char above_col[KEYBOARD_ROWS][KEYBOARD_MAX_COLS] = {
    [0] = {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4},              // digits -> space row
    [1] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},          // tab row -> digits
    [2] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13},              // home row -> tab row
    [3] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12},                 // shift row -> home row
    [4] = {0, 0, 5, 11, 11},                                        // space row -> shift row
};

// Split one slot's pairs into the pools, dropping the alternates this font has
// no glyph for. The primary is kept whatever the font says. offset is where in
// the pools this slot starts, in pairs; the pairs taken are returned.
static int split_pairs(KeyChars* out, int offset, const char* pairs,
                       GlyphSupportedFn supported, void* context) {
    int used = 0;

    out->count = 0;
    out->chars = &char_pairs[offset];

    for (const char* c = pairs; *c; ) {
        int first = UTF8_charBytes(c);
        if (first == 0 || c[first] == '\0') break;

        int second = UTF8_charBytes(c + first);
        if (second == 0) break;

        // The pool was sized for every pair in the tables and filtering only
        // ever drops one, so there is always room
        bool keep = (out->count == 0) ||
                    (supported(context, c) && supported(context, c + first));
        if (keep) {
            char* plain = char_pool[(offset + used) * 2];
            char* shifted = char_pool[(offset + used) * 2 + 1];

            memcpy(plain, c, first);
            plain[first] = '\0';
            memcpy(shifted, c + first, second);
            shifted[second] = '\0';

            char_pairs[offset + used] = (KeyChar){
                .plain   = plain,
                .shifted = shifted,
            };

            used++;
            out->count++;
        }
        c += first + second;
    }
    return used;
}

// Pairs a pairs string holds, counting only the ones that are complete. The
// glyph filter can drop some of them, so this is an upper bound.
static int pair_count(const char* pairs) {
    int count = 0;

    for (const char* c = pairs; *c; ) {
        int first = UTF8_charBytes(c);
        if (first == 0 || c[first] == '\0') break;

        int second = UTF8_charBytes(c + first);
        if (second == 0) break;

        count++;
        c += first + second;
    }
    return count;
}

// The pairs a table spells for this slot, or NULL when it names no such slot
static const char* table_pairs(const SourceMapping* table, char ansi) {
    for (; table->ansi != '\0'; table++) {
        if (table->ansi == ansi) return table->pairs;
    }
    return NULL;
}

// What the layout at index types on this slot: its own table, and the first
// layout's where it names none of its own
static const char* source_pairs(int index, char ansi) {
    const char* pairs = table_pairs(sources[index].keys, ansi);
    if (pairs || index == 0) return pairs;

    return table_pairs(sources[0].keys, ansi);
}

// Give every key its place in its row and the columns it leads to, then hand
// the rows to the geometry as NULL-terminated lists.
static void place_keys(void) {
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        float left = 0.0f;
        for (int col = 0; col < authored_lengths[row]; col++) {
            Key* key = &authored_rows[row][col];

            key->row        = row;
            key->col        = col;
            key->left       = left;
            key->col_up     = above_col[row][col];
            key->col_down   = below_col[row][col];

            left += key->width;
            row_keys[row][col] = key;
        }
        row_keys[row][authored_lengths[row]] = NULL;
        rows[row] = row_keys[row];
    }
    rows[KEYBOARD_ROWS] = NULL;

    geometry.width = KEYBOARD_ROW_WIDTH_UNITS;
    geometry.rows  = KEYBOARD_ROWS;
    geometry.keys  = rows;

    keyboard.geometry = &geometry;
    keyboard.home_row = KEYBOARD_HOME_ROW;
}

// The backspace key wears an arrow where the font has one, and a plain
// fallback where it does not
static void label_backspace(GlyphSupportedFn supported, void* context) {
    const char* label = supported(context, BACKSPACE_ARROW) ? BACKSPACE_ARROW
                                                            : BACKSPACE_FALLBACK;
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < authored_lengths[row]; col++) {
            Key* key = &authored_rows[row][col];
            if (key->action == KEY_BACKSPACE) key->label = label;
        }
    }
}

// Pairs every layout would hold if the font could draw them all: what the pools
// have to have room for
static int count_pairs(void) {
    int pairs = 0;

    for (int index = 0; index < LAYOUT_COUNT; index++) {
        for (int row = 0; row < KEYBOARD_ROWS; row++) {
            for (int col = 0; col < authored_lengths[row]; col++) {
                const Key* key = &authored_rows[row][col];
                if (key->action != KEY_TEXT) continue;

                const char* mapping = source_pairs(index, key->ansi);
                if (mapping) pairs += pair_count(mapping);
            }
        }
    }
    return pairs;
}

bool KeyboardMap_prepare(GlyphSupportedFn supported, void* context) {
    place_keys();

    keyboard.layouts      = layout_pointers;
    keyboard.layout_count = 0;

    if (!supported) return false;

    label_backspace(supported, context);

    // Sized before anything is split, so the second pass never runs short
    int pairs = count_pairs();

    // The running map stays usable until both pools are in hand
    char (*pool)[UTF8_CHAR_SIZE] = calloc((size_t)pairs * 2, UTF8_CHAR_SIZE);
    KeyChar* pool_pairs = calloc((size_t)pairs, sizeof(*char_pairs));
    if (!pool || !pool_pairs) {
        free(pool);
        free(pool_pairs);
        return false;
    }

    free(char_pool);
    free(char_pairs);
    char_pool  = pool;
    char_pairs = pool_pairs;

    int pairs_used = 0;

    for (int index = 0; index < LAYOUT_COUNT; index++) {
        KeyboardLayout* layout = &layouts[index];

        layout->name     = sources[index].name;
        layout->geometry = &geometry;
        layout->chars    = &layout_chars[index][0][0];

        // A slot the layout leaves out is split from the table it falls back
        // to, into this layout's own characters rather than a share of the
        // other's, so an index means the same character whoever reads it
        for (int row = 0; row < KEYBOARD_ROWS; row++) {
            for (int col = 0; col < KEYBOARD_MAX_COLS; col++) {
                layout_chars[index][row][col] = (KeyChars){0};
            }
            for (int col = 0; col < authored_lengths[row]; col++) {
                const Key* key = &authored_rows[row][col];
                if (key->action != KEY_TEXT) continue;

                const char* mapping = source_pairs(index, key->ansi);
                if (!mapping) continue;

                pairs_used += split_pairs(&layout_chars[index][row][col],
                                          pairs_used, mapping, supported,
                                          context);
            }
        }

        layout_pointers[index] = layout;
    }

    keyboard.layout_count = LAYOUT_COUNT;
    return true;
}

void KeyboardMap_quit(void) {
    free(char_pool);
    free(char_pairs);
    char_pool  = NULL;
    char_pairs = NULL;

    keyboard.layout_count = 0;
}

const Keyboard* KeyboardMap_get(void) {
    return &keyboard;
}

// The characters a layout holds for a key, or NULL when the key is not one this
// module placed or the layout types nothing there
static const KeyChars* chars_at(const KeyboardLayout* layout, const Key* key) {
    if (!layout || !layout->chars || !key) return NULL;
    if (key->row < 0 || key->row >= KEYBOARD_ROWS) return NULL;
    if (key->col < 0 || key->col >= KEYBOARD_MAX_COLS) return NULL;

    return &layout->chars[key->row * KEYBOARD_MAX_COLS + key->col];
}

int KeyboardMap_charCount(const KeyboardLayout* layout, const Key* key) {
    const KeyChars* chars = chars_at(layout, key);
    return chars ? chars->count : 0;
}

const char* KeyboardMap_char(const KeyboardLayout* layout, const Key* key,
                             bool shifted, int index) {
    const KeyChars* chars = chars_at(layout, key);
    if (!chars || index < 0 || index >= chars->count) return "";

    const KeyChar* c = &chars->chars[index];
    return shifted ? c->shifted : c->plain;
}
