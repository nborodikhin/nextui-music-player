#include <string.h>

#include "keyboard_map.h"
#include "utf8.h"

// Preferred backspace label, and what to draw when the font has no arrow
#define BACKSPACE_ARROW "\u2190"
#define BACKSPACE_FALLBACK "<-"

// A character key, one unit wide unless the table says otherwise
#define K(slot) KW(slot, 100)

#define KW(slot, width) {   \
    .action = KEY_TEXT,     \
    .ansi   = slot,         \
    .label  = NULL,         \
    .units  = width,        \
}

// A key that types nothing: it acts, and shows a label instead of a character
#define KEY(what, name, width) { \
    .action = what,              \
    .ansi   = '\0',              \
    .label  = name,              \
    .units  = width,             \
}

#define K_BKSP    KEY(KEY_BACKSPACE, BACKSPACE_ARROW, 200)
#define K_TAB     KEY(KEY_TAB,       "tab",           150)
#define K_CAPS    KEY(KEY_CAPS,      "cl",            175)
#define K_ENTER   KEY(KEY_ENTER,     "enter",         225)
#define K_LANG    KEY(KEY_LANG,      NULL,            250)
#define K_LSHIFT  KEY(KEY_SHIFT,     "shift",         225)
#define K_RSHIFT  KEY(KEY_SHIFT,     "shift",         275)
#define K_SPACE   KEY(KEY_SPACE,     "space",         625)
#define K_CANCEL  KEY(KEY_CANCEL,    "cancel",        250)
#define GAP_187   KEY(KEY_GAP,       NULL,            187)

// END closes a row and stands for the units left over up to the full 15u
#define END       KEY(KEY_END,       NULL,              0)

// Standard ANSI geometry, each row 15 units wide, shared by every map. Text
// keys carry the ANSI character naming their slot; the maps below say what
// each slot types. The input row holds no keys: what END pads out is the
// text field.
static const KeyboardKey geometry[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {END},

    {K('`'), K('1'), K('2'), K('3'), K('4'), K('5'), K('6'),
     K('7'), K('8'), K('9'), K('0'), K('-'), K('='), K_BKSP, END},

    {K_TAB, K('q'), K('w'), K('e'), K('r'), K('t'), K('y'),
     K('u'), K('i'), K('o'), K('p'), K('['), K(']'), KW('\\', 150), END},

    {K_CAPS, K('a'), K('s'), K('d'), K('f'), K('g'), K('h'),
     K('j'), K('k'), K('l'), K(';'), K('\''), K_ENTER, END},

    {K_LSHIFT, K('z'), K('x'), K('c'), K('v'), K('b'), K('n'),
     K('m'), K(','), K('.'), K('/'), K_RSHIFT, END},

    {K_LANG, GAP_187, K_SPACE, GAP_187, K_CANCEL, END},
};

// Each entry spells unshifted/shifted pairs. The first pair is what the key
// types and shows; the rest are its alternates, ordered by how often they come
// up across the alphabets the map serves. An alternate with no case of its own
// repeats itself.
static const KeyMapping mapping_lat[] = {
    {'`',  "`~"},
    {'1',  "1!"},  {'2', "2@"},  {'3', "3#"},  {'4', "4$"},  {'5', "5%"},
    {'6',  "6^"},  {'7', "7&"},  {'8', "8*"},  {'9', "9("},  {'0', "0)"},
    {'-',  "-_——––"},
    {'=',  "=+"},

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
    {'[',  "[{"},
    {']',  "]}"},
    {'\\', "\\|"},

    {'a',  "aAáÁàÀâÂäÄãÃåÅāĀăĂąĄæÆ"},
    {'s',  "sSšŠśŚşŞșȘßẞŝŜ"},
    {'d',  "dDďĎđĐðÐ"},
    {'f',  "fF"},
    {'g',  "gGğĞġĠĝĜģĢǧǦǥǤ"},
    {'h',  "hHħĦĥĤȟȞ"},
    {'j',  "jJĵĴ"},
    {'k',  "kKķĶǩǨ"},
    {'l',  "lLłŁľĽĺĹļĻḻḺ"},
    {';',  ";:"},
    {'\'', "'\"\"'``‘‘’’„„““””««»»ʻʻʼʼ"},

    {'z',  "zZžŽźŹżŻ"},
    {'x',  "xX"},
    {'c',  "cCçÇćĆčČċĊĉĈ"},
    {'v',  "vV"},
    {'b',  "bB"},
    {'n',  "nNñÑńŃňŇņŅŋŊṉṈ"},
    {'m',  "mM"},
    {',',  ",<"},
    {'.',  ".>……"},
    {'/',  "/?"},
};

// Russian primaries. Letters the neighbouring alphabets add - Ukrainian,
// Belarusian, Kazakh, Serbian - hang off the letter they belong to. Digits and
// punctuation this map leaves out come from the Latin one.
static const KeyMapping mapping_cyr[] = {
    {'1',  "1!ӏӀ"},  {'2', "2\""},  {'3', "3№"},  {'4', "4;"},  {'6', "6:"},  {'7', "7?"},

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
    {'/',  ".,"},
};

#define COUNT_OF(array) ((int)(sizeof(array) / sizeof((array)[0])))

// Room for every map's pairs once the font has had its say, terminators included
#define PAIRS_POOL_SIZE 2048

static const KeyboardMap maps[KEYBOARD_MAP_COUNT] = {
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
static KeyboardMap filtered_maps[KEYBOARD_MAP_COUNT];
static KeyMapping filtered_keys[KEYBOARD_MAP_COUNT][KEYBOARD_MAX_MAPPINGS];
static char pairs_pool[PAIRS_POOL_SIZE];
static int pairs_used = 0;
static bool filtered_ready = false;

// Start of the nth character of a UTF-8 string, or NULL past its end
static const char* utf8_at(const char* text, int index) {
    if (!text) return NULL;

    const char* c = text;
    for (int step = 0; step < index; step++) {
        if (*c == '\0') return NULL;
        c += UTF8_charBytes(c);
    }
    return (*c == '\0') ? NULL : c;
}

// Copy the pairs this font can draw into the pool, primary first and always
static const char* filter_pairs(const char* pairs, GlyphSupportedFn supported,
                                void* context) {
    char* out = pairs_pool + pairs_used;
    int length = 0;

    const char* c = pairs;
    for (int pair = 0; *c; pair++) {
        int first = UTF8_charBytes(c);
        if (c[first] == '\0') break;
        int second = UTF8_charBytes(c + first);

        int width = first + second;
        bool keep = (pair == 0) ||
                    (supported(context, c) && supported(context, c + first));
        if (keep && pairs_used + length + width + 1 <= PAIRS_POOL_SIZE) {
            memcpy(out + length, c, width);
            length += width;
        }
        c += width;
    }

    out[length] = '\0';
    pairs_used += length + 1;
    return out;
}

void KeyboardMap_filter(GlyphSupportedFn supported, void* context) {
    if (!supported) return;
    pairs_used = 0;

    for (int index = 0; index < KEYBOARD_MAP_COUNT; index++) {
        const KeyboardMap* source = &maps[index];
        int count = source->count;
        if (count > KEYBOARD_MAX_MAPPINGS) count = KEYBOARD_MAX_MAPPINGS;

        for (int key = 0; key < count; key++) {
            filtered_keys[index][key].ansi = source->keys[key].ansi;
            filtered_keys[index][key].pairs =
                filter_pairs(source->keys[key].pairs, supported, context);
        }

        filtered_maps[index] = (KeyboardMap){
            .name       = source->name,
            .base       = NULL,
            .base_count = 0,
            .keys       = filtered_keys[index],
            .count      = count,
        };
    }

    // A map's base is another map's filtered keys, so it inherits what survived
    for (int index = 0; index < KEYBOARD_MAP_COUNT; index++) {
        if (!maps[index].base) continue;

        for (int other = 0; other < KEYBOARD_MAP_COUNT; other++) {
            if (maps[other].keys != maps[index].base) continue;

            filtered_maps[index].base = filtered_maps[other].keys;
            filtered_maps[index].base_count = filtered_maps[other].count;
        }
    }

    filtered_ready = true;
}

const KeyboardMap* KeyboardMap_get(int index) {
    if (index < 0 || index >= KEYBOARD_MAP_COUNT) index = 0;
    return filtered_ready ? &filtered_maps[index] : &maps[index];
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

static const char* find_pairs(const KeyMapping* entries, int count, char ansi) {
    for (int index = 0; index < count; index++) {
        if (entries[index].ansi == ansi) return entries[index].pairs;
    }
    return NULL;
}

const char* KeyboardMap_pairs(const KeyboardMap* map, const KeyboardKey* key) {
    if (!map || key->action != KEY_TEXT) return NULL;

    const char* pairs = find_pairs(map->keys, map->count, key->ansi);
    if (!pairs) pairs = find_pairs(map->base, map->base_count, key->ansi);
    return pairs;
}

int KeyboardMap_variantCount(const char* pairs) {
    if (!pairs) return 0;

    int characters = 0;
    for (const char* c = pairs; *c; c += UTF8_charBytes(c)) characters++;
    return characters / 2;
}

void KeyboardMap_variant(const char* pairs, int index, bool shifted,
                         char out[KEYBOARD_TEXT_SIZE]) {
    out[0] = '\0';

    const char* c = utf8_at(pairs, index * 2 + (shifted ? 1 : 0));
    if (!c) return;

    int length = UTF8_charBytes(c);
    memcpy(out, c, length);
    out[length] = '\0';
}

// Which key of the row below each key leads to, and which of the row above.
// Column -1 means the row has no key there. The two are not mirror images:
// G and H both drop to B, and B comes back up to G; [ and ] both drop to the
// quote, which returns to [.
#define X (-1)

static const signed char below_col[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    [1] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},          // digits -> tab row
    [2] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 12},          // tab row -> home row
    [3] = {0, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11},               // home row -> shift row
    [4] = {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4},                    // shift row -> space row
    [5] = {0, X, 6, X, 13},                                        // space row -> digits
};

static const signed char above_col[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    [1] = {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4},              // digits -> space row
    [2] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},          // tab row -> digits
    [3] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13},              // home row -> tab row
    [4] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12},                 // shift row -> home row
    [5] = {0, X, 5, X, 11},                                        // space row -> shift row
};

#undef X

void KeyboardMap_step(int row, int col, int step, int* out_row, int* out_col) {
    *out_row = row;
    *out_col = col;

    // The input row holds no keys, so the grid wraps between the two ends of it
    int target = (step > 0) ? ((row >= KEYBOARD_ROWS - 1) ? 1 : row + 1)
                            : ((row <= 1) ? KEYBOARD_ROWS - 1 : row - 1);
    if (row < 1 || row >= KEYBOARD_ROWS) return;
    if (col < 0 || col >= KEYBOARD_COLS) return;

    int landing = (step > 0) ? below_col[row][col] : above_col[row][col];
    if (landing < 0 || landing >= KeyboardMap_rowLength(target)) return;

    *out_row = target;
    *out_col = landing;
}

int KeyboardMap_keyCenter(int row, int col) {
    const KeyboardKey* keys = KeyboardMap_row(row);

    int offset = 0;
    for (int index = 0; index < col && keys[index].action != KEY_END; index++) {
        offset += keys[index].units;
    }
    return offset + keys[col].units / 2;
}

// The backspace label, or its plain fallback when the font has no arrow
const char* KeyboardMap_backspaceLabel(bool font_has_arrow) {
    return font_has_arrow ? BACKSPACE_ARROW : BACKSPACE_FALLBACK;
}
