#ifndef __KEYBOARD_MAP_H__
#define __KEYBOARD_MAP_H__

#include <stdbool.h>

#include "utf8.h"

typedef enum {
    KEY_TEXT,       // regular key
    KEY_SPACE,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_CAPS,
    KEY_SHIFT,      // shift (either left or right)
    KEY_LANG,       // layout change key
    KEY_ENTER,
    KEY_CANCEL,     // abort
    KEY_SPACER,     // invisible pseudo-key, used where empty space is needed (e.g. around SPACE bar)
} KeyAction;

typedef enum {
    SHIFT_OFF,
    SHIFT_ONCE,
    SHIFT_LOCKED,
} ShiftState;

#define KEY_NO_COLUMN (-1)

// Description of a key on the keyboard, layout-independent.
// "ANSI character" is used to identify the key on the keyboard.
typedef struct {
    char        ansi;         // ANSI character the key stands for, \0 where it
                              // stands for none. A special key may still name one
                              // without typing it: tab, enter and space do.
    KeyAction   action;
    const char* label;        // drawn instead of a character, special keys only.
                              // Written in caps: it is drawn as it stands.
    int         row, col;     // where this key sits
    int         col_up;       // the column of the key above this one, KEY_NO_COLUMN if no key
    int         col_down;     // the column of the key below this one, KEY_NO_COLUMN if no key
    float       width;        // in key units, 1.0f being a regular key
    float       left;         // in key units, 1.0f being a regular key
} Key;

// The shape, size and key placement on the keyboard.
// Rows may have different number of keys in them, keys[row] is a null-terminated array.
typedef struct {
    float                    width;   // in key units
    int                      rows;
    const Key* const* const* keys;    // keys[row][col]
} KeyboardGeometry;

typedef struct KeyChars KeyChars;

// Character layout on the keyboard (e.g. LAT/CYR).
typedef struct {
    const char*             name;
    const KeyboardGeometry* geometry;
    const KeyChars*         chars;      // use char access functions
} KeyboardLayout;

typedef struct {
    const KeyboardGeometry*      geometry;
    const KeyboardLayout* const* layouts;
    int                          layout_count;
    int                          home_row;   // the row the cursor opens on
} Keyboard;

// Callback to answer whether UTF-8 character `c` could be displayed (e.g. can check the font).
typedef bool (*GlyphSupportedFn)(void* context, const char* c);

// Initialize keyboard internal data, must be called before any other keyboard
// methods. False when the map could not be built; the previous one, if any, is
// left intact.
bool KeyboardMap_prepare(GlyphSupportedFn supported, void* context);

// Release what prepare allocated. The map has no layouts until prepared again.
void KeyboardMap_quit(void);

const Keyboard* KeyboardMap_get(void);

// Number of characters a key produces - includes primary character and alternatives.
// Zero for a key that types nothing.
int KeyboardMap_charCount(const KeyboardLayout* layout, const Key* key);

// The nth character assigned to the key (the sequence of char's UTF-8 bytes).
// Empty string if not found or not applicable.
const char* KeyboardMap_char(const KeyboardLayout* layout, const Key* key,
                             bool shifted, int index);

#endif
