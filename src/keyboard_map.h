#ifndef __KEYBOARD_MAP_H__
#define __KEYBOARD_MAP_H__

#include <stdbool.h>

#include "utf8.h"

// Where the cursor opens
#define KEYBOARD_HOME_ROW 2

// Which character map a key types from. COUNT is the wrap, not a map.
typedef enum {
    KEYBOARD_MAP_LATIN,
    KEYBOARD_MAP_CYRILLIC,
    KEYBOARD_MAP_COUNT,
} KeyboardMapIndex;

typedef enum {
    KEY_END,        // row terminator
    KEY_TEXT,       // types the character its map gives this slot
    KEY_SPACE,
    KEY_BACKSPACE,
    KEY_TAB,        // no-op
    KEY_CAPS,       // shift lock
    KEY_SHIFT,      // off -> once -> locked
    KEY_LANG,       // other map
    KEY_ENTER,      // confirm
    KEY_CANCEL,     // abort
    KEY_GAP,        // laid out, not drawn
} KeyAction;

// Shift is off, on for the next character, or locked until pressed again
typedef enum {
    SHIFT_OFF,
    SHIFT_ONCE,
    SHIFT_LOCKED,
} ShiftState;

// One slot of the keyboard. The geometry is the same for every map; `ansi`
// names the slot, and each map says what it types there.
typedef struct {
    KeyAction   action;
    char        ansi;    // ANSI character this slot stands for, text keys only
    const char* label;   // drawn instead of a character, special keys only.
                         // Written in caps: it is drawn as it stands.
    float       width;   // in key units, 1.0f being a plain key
    float       left;    // from the row's left edge, filled in by _prepare()
} KeyboardKey;

// What one map types on one slot: pairs of an unshifted and a shifted
// character, the first pair primary and the rest offered on a long press.
// _prepare() splits them out of the table's pair strings, so each is a string
// of its own and nothing has to be copied out to be drawn or typed.
typedef struct {
    char ansi;
    int  count;                            // pairs, the primary included
    const char (*chars)[UTF8_CHAR_SIZE];   // 2 * count, unshifted first
} KeyMapping;

typedef struct {
    const char*       name;     // shown on the lang key
    const KeyMapping* base;     // consulted for slots this map leaves out
    int               base_count;
    const KeyMapping* keys;
    int               count;
} KeyboardMap;

// Answers whether a font can draw the UTF-8 character at c. Passed in so this
// module stays free of the font library.
typedef bool (*GlyphSupportedFn)(void* context, const char* c);

// Build the maps this font can actually draw: every primary is kept, and the
// alternates it has no glyph for are dropped. Call this before anything else
// here - the accessors serve what it builds, and hand out nothing until it has
// run. Once per process is enough.
void KeyboardMap_prepare(GlyphSupportedFn supported, void* context);

// Latin (0) or Cyrillic (1), as the font allows. Wraps out-of-range indices.
const KeyboardMap* KeyboardMap_get(int index);

// Rows of keys: digits, tab, home, shift, space. The text field is not one of
// them - it is drawn above the grid, and belongs to whoever lays the screen out.
int KeyboardMap_rowCount(void);

// The width every row spans, in key units. Rows hold different numbers of
// keys and all come out the same width: END stands for what the keys leave
// over.
int KeyboardMap_rowWidthUnits(void);

// A row of the keyboard, terminated by a KEY_END key.
const KeyboardKey* KeyboardMap_row(int row);

// Keys in a row, not counting the terminator.
int KeyboardMap_rowLength(int row);

// True for keys the cursor cannot land on.
bool KeyboardMap_isSkipped(const KeyboardKey* key);

// True for keys that type nothing - the modifiers and the space bar.
bool KeyboardMap_isSpecial(const KeyboardKey* key);

// What this map gives the key, or NULL when the key types nothing.
const KeyMapping* KeyboardMap_key(const KeyboardMap* map, const KeyboardKey* key);

// Alternates the key offers, the primary included.
int KeyboardMap_variantCount(const KeyMapping* mapping);

// The nth alternate, shifted or not. An empty string when there is no such one.
const char* KeyboardMap_variant(const KeyMapping* mapping, int index, bool shifted);

void KeyboardMap_step(int row, int col, int step, int* out_row, int* out_col);

#endif
