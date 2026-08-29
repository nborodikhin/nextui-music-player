#include "keyboard.h"
#include "defines.h"
#include "api.h"
#include "display_helper.h"
#include "help_screen.h"
#include "module_common.h"
#include "keyboard_map.h"
#include "utf8.h"
#include "ui_keyboard.h"

#include <stdlib.h>
#include <string.h>

#define KEYBOARD_MAX_INPUT 512

// How long A is held on a character key before its alternates come up.
#define KEYBOARD_HOLD_MS 250

// No alternate is being picked
#define VARIANT_NONE (-1)

// Where the cursor sits, and what it types there
typedef struct {
    int row;
    int col;
    ShiftState shift;
    int map;
} KeyboardCursor;

void Keyboard_init(void) {
    // Probe the font now rather than during the first frame
    UIKeyboard_init();
}

// A tap on shift turns it on for one character, and turns it off again from
// anywhere. Caps lock only ever comes off with a tap.
static ShiftState shift_tapped(ShiftState shift) {
    return (shift == SHIFT_OFF) ? SHIFT_ONCE : SHIFT_OFF;
}

// Caps lock is the lock alone: on from anywhere else, off from locked
static ShiftState caps_tapped(ShiftState shift) {
    return (shift == SHIFT_LOCKED) ? SHIFT_OFF : SHIFT_LOCKED;
}

static const KeyboardKey* key_at(const KeyboardCursor* cursor) {
    return &KeyboardMap_row(cursor->row)[cursor->col];
}

// Move the cursor off a gap, and back inside the row
static void cursor_rescue(KeyboardCursor* cursor) {
    if (cursor->row < 0) cursor->row = 0;
    if (cursor->row > KeyboardMap_rowCount() - 1) cursor->row = KeyboardMap_rowCount() - 1;

    int length = KeyboardMap_rowLength(cursor->row);
    if (cursor->col > length - 1) cursor->col = length - 1;
    if (cursor->col < 0) cursor->col = 0;

    const KeyboardKey* keys = KeyboardMap_row(cursor->row);
    while (cursor->col < length && KeyboardMap_isSkipped(&keys[cursor->col])) cursor->col++;
    while (cursor->col > 0 && KeyboardMap_isSkipped(&keys[cursor->col])) cursor->col--;
}

static void move_vertical(KeyboardCursor* cursor, int step) {
    KeyboardMap_step(cursor->row, cursor->col, step, &cursor->row, &cursor->col);
    cursor_rescue(cursor);
}

static void move_horizontal(KeyboardCursor* cursor, int step) {
    const KeyboardKey* keys = KeyboardMap_row(cursor->row);
    int length = KeyboardMap_rowLength(cursor->row);
    if (length <= 0) return;

    for (int attempt = 0; attempt < length; attempt++) {
        cursor->col = (cursor->col + step + length) % length;
        if (!KeyboardMap_isSkipped(&keys[cursor->col])) return;
    }
}

// Add a character, or leave the text alone when it no longer fits. Whole
// characters only, so a multibyte one is never half-written.
static void append_text(char* text, size_t limit, const char* addition) {
    size_t length = strlen(text);
    size_t addition_length = strlen(addition);
    if (length + addition_length > limit) return;

    memcpy(text + length, addition, addition_length + 1);
}

static void backspace(char* text) {
    size_t bytes = UTF8_lastCharBytes(text);
    if (bytes == 0) return;

    text[strlen(text) - bytes] = '\0';
}

char* Keyboard_open(const char* prompt, size_t max_bytes) {
    DisplayContext* display = DisplayHelper_current();

    size_t limit = KEYBOARD_MAX_INPUT - 1;
    if (max_bytes > 0 && max_bytes < limit) limit = max_bytes;

    // Opens on "a": start of the home row, one step from caps and shift
    KeyboardCursor cursor = {
        .row   = KEYBOARD_HOME_ROW,
        .col   = 1,
        .shift = SHIFT_OFF,
        .map   = KEYBOARD_MAP_LATIN,
    };
    char text[KEYBOARD_MAX_INPUT] = "";
    bool confirmed = false;
    bool done = false;

    // A character key types on release, so holding it can bring up its
    // alternates instead
    bool holding = false;
    bool pressed = false;

    // Set once a hold has done its work, so the release does not act again
    bool hold_consumed = false;

    // A direction pressed while a held key would do something unwanted takes
    // the press back: the way out of an A landing on enter or cancel by
    // accident. Typing a character or a space is not worth undoing.
    bool cancelled = false;
    uint32_t x_held_since = 0;
    bool x_consumed = false;

    // A button whose press went to a dialog must not act here when it comes
    // back up: closing the controls help with X is not a shift
    bool swallow_buttons = false;
    uint32_t held_since = 0;
    int variant = VARIANT_NONE;

    int dirty = 1;
    int show_setting = 0;

    while (!done) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);

        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting,
                                                                  HELP_KEYBOARD);
        if (global.should_quit) {
            // The app is quitting; the caller sees it on its own next frame
            return NULL;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            swallow_buttons = true;
            GFX_sync();
            continue;
        }

        // Wait for the pad to come up before reading it again
        if (swallow_buttons) {
            if (PAD_isPressed(BTN_A) || PAD_isPressed(BTN_B) || PAD_isPressed(BTN_X) ||
                PAD_isPressed(BTN_Y) || PAD_isPressed(BTN_SELECT)) {
                GFX_sync();
                continue;
            }
            swallow_buttons = false;
        }

        // A key drawn as held has to be redrawn when it comes back up, and a
        // special key acts on the press, so nothing else marks that frame dirty
        bool held = PAD_isPressed(BTN_A) && !cancelled;
        if (held != pressed) {
            pressed = held;
            dirty = 1;
        }

        const KeyboardKey* key = key_at(&cursor);
        const KeyMapping* mapping = KeyboardMap_key(KeyboardMap_get(cursor.map), key);
        int variant_count = KeyboardMap_variantCount(mapping);

        // A held button owns the pad, except while the alternates are up, where
        // left and right pick between them
        bool button_held = PAD_isPressed(BTN_A) || PAD_isPressed(BTN_B) ||
                           PAD_isPressed(BTN_X) || PAD_isPressed(BTN_Y) ||
                           PAD_isPressed(BTN_SELECT);

        if (variant >= 0) {
            if (PAD_justRepeated(BTN_LEFT) && variant > 0) {
                variant--;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_RIGHT) && variant < variant_count - 1) {
                variant++;
                dirty = 1;
            }
        }
        else if (!button_held) {
            if (PAD_justRepeated(BTN_UP)) {
                move_vertical(&cursor, -1);
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN)) {
                move_vertical(&cursor, 1);
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_LEFT)) {
                move_horizontal(&cursor, -1);
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_RIGHT)) {
                move_horizontal(&cursor, 1);
                dirty = 1;
            }
        }

        bool cancellable = (key->action != KEY_TEXT && key->action != KEY_SPACE);

        if (holding && !cancelled && cancellable &&
            (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_DOWN) ||
             PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT))) {
            cancelled = true;
            hold_consumed = true;
            dirty = 1;
        }
        else if (PAD_justRepeated(BTN_A) && !cancelled &&
                 key->action == KEY_BACKSPACE) {
            // The one key that works like the B button: it deletes on the way
            // down and keeps deleting while it is held. The press is still a
            // hold as far as the rest of the loop is concerned, so a direction
            // can call it off; its release has nothing left to do.
            if (PAD_justPressed(BTN_A)) {
                holding = true;
                hold_consumed = true;
                cancelled = false;
                held_since = SDL_GetTicks();
            }
            backspace(text);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            // Every other key acts on release, so a hold can mean something
            // else - the alternates on a character key, the lock on shift
            holding = true;
            hold_consumed = false;
            cancelled = false;
            held_since = SDL_GetTicks();
            dirty = 1;
        }
        else if (holding && !PAD_isPressed(BTN_A)) {
            if (hold_consumed) {
                holding = false;
                cancelled = false;
                variant = VARIANT_NONE;
                dirty = 1;
                GFX_sync();
                continue;
            }

            switch (key->action) {
                case KEY_TEXT: {
                    const char* typed = KeyboardMap_variant(
                        mapping, variant > 0 ? variant : 0, cursor.shift != SHIFT_OFF);
                    append_text(text, limit, typed);
                    if (cursor.shift == SHIFT_ONCE) cursor.shift = SHIFT_OFF;
                    break;
                }
                case KEY_SPACE:
                    append_text(text, limit, " ");
                    if (cursor.shift == SHIFT_ONCE) cursor.shift = SHIFT_OFF;
                    break;
                case KEY_CAPS:
                    cursor.shift = caps_tapped(cursor.shift);
                    break;
                case KEY_SHIFT:
                    cursor.shift = shift_tapped(cursor.shift);
                    break;
                case KEY_LANG:
                    cursor.map = (cursor.map + 1) % KEYBOARD_MAP_COUNT;
                    break;
                case KEY_ENTER:
                    confirmed = true;
                    done = true;
                    break;
                case KEY_CANCEL:
                    done = true;
                    break;
                case KEY_BACKSPACE:   // already deleted on the way down
                case KEY_TAB:
                case KEY_GAP:
                case KEY_END:
                    break;
            }

            holding = false;
            variant = VARIANT_NONE;
            dirty = 1;
        }
        else if (holding && !hold_consumed && variant < 0 &&
                 SDL_GetTicks() - held_since >= KEYBOARD_HOLD_MS) {
            // Once: reopening every frame would put the pick back on the first
            // alternate as fast as Left and Right could move it
            if (key->action == KEY_TEXT) {
                // Even a key with a single character opens, so the gesture is
                // the same wherever the cursor is
                variant = 0;
                dirty = 1;
            }
        }
        else if (PAD_justPressed(BTN_B) && text[0] == '\0') {
            // B backs out of an empty field, the way it leaves any other screen
            done = true;
        }
        else if (PAD_justRepeated(BTN_B)) {
            // Held down it keeps deleting, and stops at the empty field rather
            // than leaving through it
            if (text[0] != '\0') {
                backspace(text);
                dirty = 1;
            }
        }
        else if (PAD_justPressed(BTN_X)) {
            x_held_since = SDL_GetTicks();
            x_consumed = false;
        }
        else if (PAD_isPressed(BTN_X) && !x_consumed &&
                 SDL_GetTicks() - x_held_since >= KEYBOARD_HOLD_MS) {
            // Held, X is the caps key
            cursor.shift = caps_tapped(cursor.shift);
            x_consumed = true;
            dirty = 1;
        }
        else if (PAD_justReleased(BTN_X)) {
            if (!x_consumed) {
                cursor.shift = shift_tapped(cursor.shift);
                dirty = 1;
            }
            x_consumed = false;
        }
        else if (PAD_justPressed(BTN_Y)) {
            cursor.map = (cursor.map + 1) % KEYBOARD_MAP_COUNT;
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_SELECT)) {
            confirmed = true;
            done = true;
        }

        if (done) break;

        if (dirty) {
            UIKeyboard_render(screen, prompt, text, cursor.map, cursor.shift,
                              cursor.row, cursor.col, pressed, variant, show_setting);
            GFX_flip(screen);
            dirty = 0;
        }
        else {
            GFX_sync();
        }
    }

    if (!confirmed || text[0] == '\0') return NULL;

    char* result = malloc(strlen(text) + 1);
    if (!result) return NULL;
    strcpy(result, text);
    return result;
}
