#ifndef __KEYBOARD_MAP_H__
#define __KEYBOARD_MAP_H__

#include <stdbool.h>

#define KEYBOARD_ROWS 6

// The top row holds the text field alone; the cursor opens on the home row
#define KEYBOARD_INPUT_ROW 0
#define KEYBOARD_HOME_ROW 3
#define KEYBOARD_COLS 16

// Latin and Cyrillic
#define KEYBOARD_MAP_LATIN 0
#define KEYBOARD_MAP_CYRILLIC 1
#define KEYBOARD_MAP_COUNT 2

// Longest UTF-8 character plus its terminator
#define KEYBOARD_TEXT_SIZE 5

// Longest key label plus its terminator
#define KEYBOARD_LABEL_SIZE 12

// Most alternates one key offers
#define KEYBOARD_MAX_VARIANTS 12

// Most slots a map fills
#define KEYBOARD_MAX_MAPPINGS 64

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
    const char* label;   // drawn instead of a character, special keys only
    int         units;   // width in hundredths of a key unit
} KeyboardKey;

// What one map types on one slot: unshifted/shifted pairs, the first pair
// primary and the rest offered on a long press.
typedef struct {
    char        ansi;
    const char* pairs;
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

// Drop the alternates the font cannot draw, keeping every primary. Called once
// per process; the accessors below serve the filtered maps afterwards.
void KeyboardMap_filter(GlyphSupportedFn supported, void* context);

// Latin (0) or Cyrillic (1), as the font allows. Wraps out-of-range indices.
const KeyboardMap* KeyboardMap_get(int index);

// A row of the keyboard, terminated by a KEY_END key.
const KeyboardKey* KeyboardMap_row(int row);

// Keys in a row, not counting the terminator.
int KeyboardMap_rowLength(int row);

// True for keys the cursor cannot land on.
bool KeyboardMap_isSkipped(const KeyboardKey* key);

// True for keys that type nothing - the modifiers and the space bar.
bool KeyboardMap_isSpecial(const KeyboardKey* key);

// The pairs this map gives the key, or NULL when it types nothing.
const char* KeyboardMap_pairs(const KeyboardMap* map, const KeyboardKey* key);

// Alternates a pairs string holds, the primary included. A trailing character
// without its pair is ignored.
int KeyboardMap_variantCount(const char* pairs);

// The nth alternate, shifted or not, written to out. Empty when out of range.
void KeyboardMap_variant(const char* pairs, int index, bool shifted,
                         char out[KEYBOARD_TEXT_SIZE]);

// The backspace key's label: an arrow, or a plain fallback for a font without it.
const char* KeyboardMap_backspaceLabel(bool font_has_arrow);

// Where up or down from this key lands: step is -1 or 1, and the answer wraps
// between the digit row and the space row. The rows have different key counts,
// so the neighbours are spelled out rather than computed.
void KeyboardMap_step(int row, int col, int step, int* out_row, int* out_col);

// Horizontal centre of a key, in hundredths of a unit from the row's left edge.
// Vertical navigation uses it to stay under the same key across rows.
int KeyboardMap_keyCenter(int row, int col);

#endif
