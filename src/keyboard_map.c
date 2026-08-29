#include <string.h>

#include "keyboard_map.h"
#include "utf8.h"

// Room for the longest row, its END terminator included
#define KEYBOARD_COLS 16

// Digits, tab, home, shift, space
#define KEYBOARD_ROWS 5

// Every row is authored to this width; END pads out what its keys leave over
#define KEYBOARD_ROW_WIDTH_UNITS 15

// Preferred backspace label, and what to draw when the font has no arrow
#define BACKSPACE_ARROW "\u2190"
#define BACKSPACE_FALLBACK "<-"

// A character key, one unit wide unless the table says otherwise
#define K(slot) KW(slot, 1.0f)

#define KW(slot, key_width) {   \
    .action = KEY_TEXT,         \
    .ansi   = slot,             \
    .label  = NULL,             \
    .width  = key_width,        \
}

// A key that types nothing: it acts, and shows a label instead of a character
#define KEY(what, name, key_width) { \
    .action = what,                  \
    .ansi   = '\0',                  \
    .label  = name,                  \
    .width  = key_width,             \
}

#define K_BKSP    KEY(KEY_BACKSPACE, BACKSPACE_ARROW, 2.0f)
#define K_TAB     KEY(KEY_TAB,       "TAB",            1.5f)
#define K_CAPS    KEY(KEY_CAPS,      "CL",             1.75f)
#define K_ENTER   KEY(KEY_ENTER,     "ENTER",          2.25f)
#define K_LANG    KEY(KEY_LANG,      NULL,             2.5f)
#define K_LSHIFT  KEY(KEY_SHIFT,     "SHIFT",          2.25f)
#define K_RSHIFT  KEY(KEY_SHIFT,     "SHIFT",          2.75f)
#define K_SPACE   KEY(KEY_SPACE,     "SPACE",          6.25f)
#define K_CANCEL  KEY(KEY_CANCEL,    "CANCEL",         2.5f)
#define GAP_1875  KEY(KEY_GAP,       NULL,             1.875f)

// END closes a row and stands for the units left over up to the full 15u
#define END       KEY(KEY_END,       NULL,             0.0f)

// Standard ANSI geometry, each row 15 units wide, shared by every map. Not
// const: _prepare() walks it once to give every key its place in its row. Text
// keys carry the ANSI character naming their slot; the maps below say what
// each slot types. The input row holds no keys: what END pads out is the
// text field.
static KeyboardKey geometry[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {K('`'), K('1'), K('2'), K('3'), K('4'), K('5'), K('6'),
     K('7'), K('8'), K('9'), K('0'), K('-'), K('='), K_BKSP, END},

    {K_TAB, K('q'), K('w'), K('e'), K('r'), K('t'), K('y'),
     K('u'), K('i'), K('o'), K('p'), K('['), K(']'), KW('\\', 1.5f), END},

    {K_CAPS, K('a'), K('s'), K('d'), K('f'), K('g'), K('h'),
     K('j'), K('k'), K('l'), K(';'), K('\''), K_ENTER, END},

    {K_LSHIFT, K('z'), K('x'), K('c'), K('v'), K('b'), K('n'),
     K('m'), K(','), K('.'), K('/'), K_RSHIFT, END},

    {K_LANG, GAP_1875, K_SPACE, GAP_1875, K_CANCEL, END},
};

// Each entry spells unshifted/shifted pairs. The first pair is what the key
// types and shows; the rest are its alternates, ordered by how often they come
// up across the alphabets the map serves. An alternate with no case of its own
// repeats itself.
// One slot as the tables spell it: unshifted/shifted pairs in one string
typedef struct {
    char        ansi;
    const char* pairs;
} SourceMapping;

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
};

// Russian primaries. Letters the neighbouring alphabets add - Ukrainian,
// Belarusian, Kazakh, Serbian - hang off the letter they belong to. Digits and
// punctuation this map leaves out come from the Latin one.
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
};

#define COUNT_OF(array) ((int)(sizeof(array) / sizeof((array)[0])))

// Most slots a map fills: what prepared_keys has room for
#define KEYBOARD_MAX_MAPPINGS 64

// Every character of every map, split out of the pair strings and kept as its
// own string. The tables hold about 650 of them; the rest is room to grow.
#define CHAR_POOL_SLOTS 1024

// The source tables, before the font has had its say
typedef struct {
    const char*          name;
    const SourceMapping* base;
    int                  base_count;
    const SourceMapping* keys;
    int                  count;
} SourceMap;

static const SourceMap sources[KEYBOARD_MAP_COUNT] = {
    {
        .name       = "LAT",
        .base       = NULL,
        .base_count = 0,
        .keys       = mapping_lat,
        .count      = COUNT_OF(mapping_lat),
    },
    {
        .name       = "CYR",
        .base       = mapping_lat,
        .base_count = COUNT_OF(mapping_lat),
        .keys       = mapping_cyr,
        .count      = COUNT_OF(mapping_cyr),
    },
};

// The maps as the font can actually draw them: every primary kept, alternates
// only where the glyphs exist. A primary the font lacks stays put and comes out
// as the font's own tofu, which says more than a missing key would.
static KeyboardMap prepared_maps[KEYBOARD_MAP_COUNT];
static KeyMapping prepared_keys[KEYBOARD_MAP_COUNT][KEYBOARD_MAX_MAPPINGS];
static char char_pool[CHAR_POOL_SLOTS][UTF8_CHAR_SIZE];
static int chars_used = 0;

// Split one key's pairs into the pool, dropping the alternates this font has no
// glyph for. The primary is kept whatever the font says.
static void split_pairs(KeyMapping* out, const char* pairs,
                        GlyphSupportedFn supported, void* context) {
    out->count = 0;
    out->chars = (const char (*)[UTF8_CHAR_SIZE])char_pool[chars_used];

    for (const char* c = pairs; *c; ) {
        int first = UTF8_charBytes(c);
        if (first == 0 || c[first] == '\0') break;

        int second = UTF8_charBytes(c + first);
        if (second == 0) break;

        bool keep = (out->count == 0) ||
                    (supported(context, c) && supported(context, c + first));
        // A full pool would silently shorten a key; the tests count what the
        // tables hold, so this is a build-time problem, not a runtime one
        if (chars_used + 2 > CHAR_POOL_SLOTS) break;
        if (keep) {
            memcpy(char_pool[chars_used], c, first);
            char_pool[chars_used][first] = '\0';
            memcpy(char_pool[chars_used + 1], c + first, second);
            char_pool[chars_used + 1][second] = '\0';

            chars_used += 2;
            out->count++;
        }
        c += first + second;
    }
}

// The backspace key wears an arrow where the font has one, and a plain
// fallback where it does not
static void label_backspace(GlyphSupportedFn supported, void* context) {
    const char* label = supported(context, BACKSPACE_ARROW) ? BACKSPACE_ARROW
                                                            : BACKSPACE_FALLBACK;
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; geometry[row][col].action != KEY_END; col++) {
            if (geometry[row][col].action == KEY_BACKSPACE) geometry[row][col].label = label;
        }
    }
}

// Where each key starts, so nothing has to add up the row to draw one
static void place_keys(void) {
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        float left = 0.0f;
        for (int col = 0; geometry[row][col].action != KEY_END; col++) {
            geometry[row][col].left = left;
            left += geometry[row][col].width;
        }
    }
}

void KeyboardMap_prepare(GlyphSupportedFn supported, void* context) {
    place_keys();

    if (!supported) return;

    label_backspace(supported, context);
    chars_used = 0;

    for (int index = 0; index < KEYBOARD_MAP_COUNT; index++) {
        const SourceMap* source = &sources[index];
        int count = source->count;
        if (count > KEYBOARD_MAX_MAPPINGS) count = KEYBOARD_MAX_MAPPINGS;

        for (int key = 0; key < count; key++) {
            prepared_keys[index][key].ansi = source->keys[key].ansi;
            split_pairs(&prepared_keys[index][key], source->keys[key].pairs,
                        supported, context);
        }

        prepared_maps[index] = (KeyboardMap){
            .name       = source->name,
            .base       = NULL,
            .base_count = 0,
            .keys       = prepared_keys[index],
            .count      = count,
        };
    }

    // A map's base is another map's prepared keys, so it inherits what survived
    for (int index = 0; index < KEYBOARD_MAP_COUNT; index++) {
        if (!sources[index].base) continue;

        for (int other = 0; other < KEYBOARD_MAP_COUNT; other++) {
            if (sources[other].keys != sources[index].base) continue;

            prepared_maps[index].base = prepared_maps[other].keys;
            prepared_maps[index].base_count = prepared_maps[other].count;
        }
    }
}

// Nothing can be drawn before the font has been probed: the maps served here
// are the split ones, and _prepare() is what fills them.
const KeyboardMap* KeyboardMap_get(int index) {
    if (index < 0 || index >= KEYBOARD_MAP_COUNT) index = 0;
    return &prepared_maps[index];
}

int KeyboardMap_rowCount(void) {
    return KEYBOARD_ROWS;
}

int KeyboardMap_rowWidthUnits(void) {
    return KEYBOARD_ROW_WIDTH_UNITS;
}

const KeyboardKey* KeyboardMap_row(int row) {
    if (row < 0 || row >= KEYBOARD_ROWS) row = 0;
    return geometry[row];
}

int KeyboardMap_rowLength(int row) {
    const KeyboardKey* keys = KeyboardMap_row(row);

    int length = 0;
    while (length < KEYBOARD_COLS && keys[length].action != KEY_END) length++;
    return length;
}

bool KeyboardMap_isSkipped(const KeyboardKey* key) {
    return key->action == KEY_GAP || key->action == KEY_END;
}

bool KeyboardMap_isSpecial(const KeyboardKey* key) {
    return key->action != KEY_TEXT;
}

static const KeyMapping* find_key(const KeyMapping* entries, int count, char ansi) {
    for (int index = 0; index < count; index++) {
        if (entries[index].ansi == ansi) return &entries[index];
    }
    return NULL;
}

const KeyMapping* KeyboardMap_key(const KeyboardMap* map, const KeyboardKey* key) {
    if (!map || key->action != KEY_TEXT) return NULL;

    const KeyMapping* mapping = find_key(map->keys, map->count, key->ansi);
    if (!mapping) mapping = find_key(map->base, map->base_count, key->ansi);
    return mapping;
}

int KeyboardMap_variantCount(const KeyMapping* mapping) {
    return mapping ? mapping->count : 0;
}

const char* KeyboardMap_variant(const KeyMapping* mapping, int index, bool shifted) {
    if (!mapping || index < 0 || index >= mapping->count) return "";

    return mapping->chars[index * 2 + (shifted ? 1 : 0)];
}

// Which key of the row below each key leads to, and which of the row above.
// Column -1 means the row has no key there. The two are not mirror images:
// G and H both drop to B, and B comes back up to G; [ and ] both drop to the
// quote, which returns to [.
#define X (-1)

static const signed char below_col[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    [0] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},          // digits -> tab row
    [1] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 12},          // tab row -> home row
    [2] = {0, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11},               // home row -> shift row
    [3] = {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4},                    // shift row -> space row
    [4] = {0, X, 6, X, 13},                                        // space row -> digits
};

static const signed char above_col[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    [0] = {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4},              // digits -> space row
    [1] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},          // tab row -> digits
    [2] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13},              // home row -> tab row
    [3] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12},                 // shift row -> home row
    [4] = {0, X, 5, X, 11},                                        // space row -> shift row
};

#undef X

void KeyboardMap_step(int row, int col, int step, int* out_row, int* out_col) {
    *out_row = row;
    *out_col = col;

    if (row < 0 || row >= KEYBOARD_ROWS) return;

    int target = (row + step + KEYBOARD_ROWS) % KEYBOARD_ROWS;
    if (col < 0 || col >= KEYBOARD_COLS) return;

    int landing = (step > 0) ? below_col[row][col] : above_col[row][col];
    if (landing < 0 || landing >= KeyboardMap_rowLength(target)) return;

    *out_row = target;
    *out_col = landing;
}
