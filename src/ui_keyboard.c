#include <math.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "keyboard_map.h"
#include "utf8.h"
#include "ui_keyboard.h"
#include "ui_fonts.h"
#include "ui_utils.h"

// Gap between keys, and between the text field and the grid
#define KEY_SPACING 4
#define INPUT_GAP 8

// Share of a key's height the glyphs take
#define KEY_FONT_RATIO 55

// Special keys are set in small caps: uppercase, at this share of the key font
#define LABEL_FONT_RATIO 78

// Keys that type nothing sit this far from the plain key color towards the
// selected one
#define KEY_BG_SPECIAL_MIX 20

// The lock LED, as a share of the key height, and its inset from the corner
#define LOCK_LED_RATIO 14
#define LOCK_LED_INSET 3

// A pressed key shrinks inside its cell
#define KEY_PRESSED_RATIO 90

// The alternates are keys like any other, on a panel that separates them from
// the grid underneath
#define VARIANT_PANEL_PADDING 1

// One key unit, in the hundredths the table is written in
#define UNIT 100

// Preferred backspace label, and what to draw when the font has no arrow
#define BACKSPACE_ARROW_CODEPOINT 0x2190
#define BACKSPACE_FALLBACK "<-"

// Every ANSI row is 15 units wide; END pads out whatever a row leaves over
#define ROW_UNITS 1500

// Where the grid sits, derived from the space between header and button hints
typedef struct {
    int       unit;        // width and height of one key unit
    int       spacing;     // taken out of a cell, not added between them
    int       board_x;     // left edge of every row
    int       board_w;
    int       grid_y;      // top of the input row
    int       input_gap;   // extra space under the input row
    TTF_Font* font;        // key characters
    TTF_Font* label_font;  // the small caps on special keys
} KeyboardMetrics;

// The codepoint of the UTF-8 character starting at c
static Uint32 utf8_codepoint(const char* c) {
    unsigned char lead = (unsigned char)*c;
    int length = UTF8_charBytes(c);
    if (length == 0) return 0;

    Uint32 code = (length == 1) ? lead
                : (length == 2) ? (lead & 0x1F)
                : (length == 3) ? (lead & 0x0F)
                                : (lead & 0x07);
    for (int index = 1; index < length; index++) {
        code = (code << 6) | ((unsigned char)c[index] & 0x3F);
    }
    return code;
}

// Whether the app font can draw this character, for the map filter
static bool font_has(void* context, const char* c) {
    TTF_Font* font = (TTF_Font*)context;
    if (!font) return true;

    // Every character the maps use is in the BMP, so the 16-bit lookup covers
    // them; the device's SDL_ttf is too old for the 32-bit one
    Uint32 code = utf8_codepoint(c);
    if (code > 0xFFFF) return false;
    return TTF_GlyphIsProvided(font, (Uint16)code) != 0;
}

void UIKeyboard_init(void) {
    KeyboardMap_filter(font_has, Fonts_getMedium());
}

// Units a row spells out, before END pads it to the full width
static int row_units(int row) {
    const KeyboardKey* keys = KeyboardMap_row(row);

    int units = 0;
    for (int index = 0; keys[index].action != KEY_END; index++) units += keys[index].units;
    return units;
}

static KeyboardMetrics compute_metrics(SDL_Surface* screen) {
    KeyboardMetrics m;
    m.spacing = SCALE1(KEY_SPACING);

    // The keyboard owns everything between the title pill and the button hints.
    // Both pills are PILL_SIZE tall a PADDING from their edge, so the band is
    // symmetric and a centred keyboard sits evenly inside it.
    int top = SCALE1(PADDING + PILL_SIZE);
    int bottom = screen->h - SCALE1(PADDING + PILL_SIZE);
    int available_h = bottom - top;

    // The input row is one unit tall like the key rows, plus the gap under it
    int unit_h = (available_h - SCALE1(INPUT_GAP)) / KEYBOARD_ROWS;

    // Every row is the full width. Keys are drawn inset inside their cells
    // rather than spaced apart, so rows stay aligned however many keys they hold.
    int unit_w = screen->w * UNIT / ROW_UNITS;

    // One unit drives both axes, so a height-limited board shrinks whole
    m.unit = MIN(unit_h, unit_w);
    m.font = Fonts_getSized(m.unit * KEY_FONT_RATIO / 100);
    m.label_font = Fonts_getSized(m.unit * KEY_FONT_RATIO * LABEL_FONT_RATIO / 10000);

    m.input_gap = SCALE1(INPUT_GAP);
    m.board_w = ROW_UNITS * m.unit / UNIT;
    m.board_x = (screen->w - m.board_w) / 2;

    int block_h = KEYBOARD_ROWS * m.unit + m.input_gap;
    m.grid_y = top + (available_h - block_h) / 2;

    return m;
}

// Top of a row's cells. The keys keep their distance from the input row.
static int row_y(const KeyboardMetrics* m, int row) {
    return m->grid_y + row * m->unit + (row > 0 ? m->input_gap : 0);
}

// Trim a cell to the drawn key, so the gaps come out of the cells and every
// row still spans the same width
static SDL_Rect inset(const KeyboardMetrics* m, SDL_Rect cell) {
    int margin = m->spacing / 2;
    return (SDL_Rect){
        .x = cell.x + margin,
        .y = cell.y + margin,
        .w = cell.w - 2 * margin,
        .h = cell.h - 2 * margin,
    };
}

static int key_width(const KeyboardMetrics* m, const KeyboardKey* key) {
    return key->units * m->unit / UNIT;
}

static SDL_Rect key_rect(const KeyboardMetrics* m, int row, int col) {
    const KeyboardKey* keys = KeyboardMap_row(row);

    int units = 0;
    for (int index = 0; index < col; index++) units += keys[index].units;

    return inset(m, (SDL_Rect){m->board_x + units * m->unit / UNIT, row_y(m, row),
                               key_width(m, &keys[col]), m->unit});
}

// The stretch END pads out at the end of a row, empty on every row but the
// input one, where it is the text field
static SDL_Rect row_tail_rect(const KeyboardMetrics* m, int row) {
    int units = row_units(row);

    return inset(m, (SDL_Rect){m->board_x + units * m->unit / UNIT, row_y(m, row),
                               (ROW_UNITS - units) * m->unit / UNIT, m->unit});
}

// Blend two theme colors, percent of the way from first to second, and map the
// result for this screen
static uint32_t mix_colors(SDL_Surface* screen, uint32_t first, uint32_t second, int percent) {
    SDL_Color a = uintToColour(first);
    SDL_Color b = uintToColour(second);

    return SDL_MapRGB(screen->format,
                      a.r + (b.r - a.r) * percent / 100,
                      a.g + (b.g - a.g) * percent / 100,
                      a.b + (b.b - a.b) * percent / 100);
}

// Filled circle, drawn as horizontal spans - the lock lamp on a shift key
static void fill_circle(SDL_Surface* screen, int cx, int cy, int radius, uint32_t color) {
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)(sqrt((double)(radius * radius - dy * dy)) + 0.5);
        SDL_FillRect(screen, &(SDL_Rect){cx - dx, cy + dy, dx * 2, 1}, color);
    }
}

// A pressed key shrinks inside its cell, the way a real key travels down
// under a finger
static SDL_Rect press(SDL_Rect rect) {
    int w = rect.w * KEY_PRESSED_RATIO / 100;
    int h = rect.h * KEY_PRESSED_RATIO / 100;

    return (SDL_Rect){
        .x = rect.x + (rect.w - w) / 2,
        .y = rect.y + (rect.h - h) / 2,
        .w = w,
        .h = h,
    };
}

// What a key shows: its label, or the character it types on this map.
// Special keys are set in small caps, so their label comes back uppercased.
static const char* key_label(const KeyboardMap* map, const KeyboardMap* other,
                             const KeyboardKey* key, bool shifted, TTF_Font* font,
                             char out[KEYBOARD_LABEL_SIZE]) {
    const char* label = key->label;

    // The lang key names where it takes you, the way a language bar does
    if (key->action == KEY_LANG && !label) label = other->name;

    if (label) {
        if (key->action == KEY_BACKSPACE) {
            label = KeyboardMap_backspaceLabel(
                !font || TTF_GlyphIsProvided(font, BACKSPACE_ARROW_CODEPOINT));
        }

        int index = 0;
        for (; label[index] != '\0' && index < KEYBOARD_LABEL_SIZE - 1; index++) {
            char c = label[index];
            out[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        out[index] = '\0';
        return out;
    }

    KeyboardMap_variant(KeyboardMap_pairs(map, key), 0, shifted, out);
    return out;
}

static void draw_label(SDL_Surface* screen, TTF_Font* font, const char* label,
                       SDL_Color color, SDL_Rect rect) {
    if (!font || !label || label[0] == '\0') return;

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, label, color);
    if (!surface) return;

    SDL_BlitSurface(surface, NULL, screen,
                    &(SDL_Rect){rect.x + (rect.w - surface->w) / 2,
                                rect.y + (rect.h - surface->h) / 2});
    SDL_FreeSurface(surface);
}

static void render_input(SDL_Surface* screen, const KeyboardMetrics* m,
                         const SDL_Rect* field, const char* text) {
    render_rounded_rect_bg(screen, field->x, field->y, field->w, field->h, THEME_COLOR1);
    if (!text || text[0] == '\0' || !m->font) return;

    // A long entry scrolls out to the left, so the caret end stays visible
    int text_w = 0;
    TTF_SizeUTF8(m->font, text, &text_w, NULL);
    int inner_x = field->x + SCALE1(BUTTON_PADDING);
    int inner_w = field->w - SCALE1(BUTTON_PADDING * 2);

    SDL_Surface* surface = TTF_RenderUTF8_Blended(m->font, text,
                                                  uintToColour(THEME_COLOR5_255));
    if (!surface) return;

    SDL_Rect clip = {0, 0, surface->w, surface->h};
    if (text_w > inner_w) {
        clip.x = text_w - inner_w;
        clip.w = inner_w;
    }
    SDL_BlitSurface(surface, &clip, screen,
                    &(SDL_Rect){inner_x, field->y + (field->h - surface->h) / 2});
    SDL_FreeSurface(surface);
}

// The alternates of the held key, in a row over it - below instead when the
// key sits against the top of the grid. The row carries its own panel, so it
// reads as sitting above the keyboard rather than among the keys.
static void render_variants(SDL_Surface* screen, const KeyboardMetrics* m,
                            const KeyboardMap* map, const KeyboardKey* key,
                            SDL_Rect anchor, bool shifted, int selected) {
    const char* pairs = KeyboardMap_pairs(map, key);
    int count = KeyboardMap_variantCount(pairs);
    if (count <= 0) return;

    // The strip is a keyboard row at the pressed key's scale, pitch included,
    // so one ratio governs the cells and the gaps between them
    int pitch = m->unit * KEY_PRESSED_RATIO / 100;
    int padding = m->spacing * VARIANT_PANEL_PADDING;
    int panel_w = pitch * count + padding * 2;
    int panel_h = pitch + padding * 2;

    // Centred on the key, then pushed back inside the screen
    int panel_x = anchor.x + anchor.w / 2 - panel_w / 2;
    if (panel_x < 0) panel_x = 0;
    if (panel_x + panel_w > screen->w) panel_x = screen->w - panel_w;

    // Above the key, or below it when there is no room over the top row
    int panel_y = anchor.y - padding - panel_h;
    if (panel_y < m->grid_y) panel_y = anchor.y + anchor.h + padding;

    render_rounded_rect_bg(screen, panel_x, panel_y, panel_w, panel_h, THEME_COLOR1);

    for (int index = 0; index < count; index++) {
        // Drawn like a key under a finger, since that is what they are
        SDL_Rect cell = inset(m, (SDL_Rect){
            .x = panel_x + padding + index * pitch,
            .y = panel_y + padding,
            .w = pitch,
            .h = pitch,
        });
        bool active = (index == selected);
        if (active) {
            render_rounded_rect_bg(screen, cell.x, cell.y, cell.w, cell.h, THEME_COLOR3);
        }

        char label[KEYBOARD_TEXT_SIZE];
        KeyboardMap_variant(pairs, index, shifted, label);
        draw_label(screen, m->font, label,
                   active ? Fonts_getListTextColor(false)
                          : uintToColour(THEME_COLOR5_255),
                   cell);
    }
}

void UIKeyboard_render(SDL_Surface* screen, const char* prompt, const char* text,
                       int map_index, ShiftState shift, int row, int col, bool pressed,
                       int variant, int show_setting) {
    GFX_clear(screen);
    render_screen_header(screen, prompt ? prompt : "", show_setting);

    const KeyboardMap* map = KeyboardMap_get(map_index);
    const KeyboardMap* other = KeyboardMap_get((map_index + 1) % KEYBOARD_MAP_COUNT);
    bool shifted = (shift != SHIFT_OFF);
    KeyboardMetrics m = compute_metrics(screen);

    SDL_Rect field = row_tail_rect(&m, KEYBOARD_INPUT_ROW);
    render_input(screen, &m, &field, text);

    SDL_Rect cursor_rect = {0, 0, 0, 0};
    const KeyboardKey* cursor_key = NULL;

    for (int r = 0; r < KEYBOARD_ROWS; r++) {
        const KeyboardKey* keys = KeyboardMap_row(r);
        int length = KeyboardMap_rowLength(r);

        for (int c = 0; c < length; c++) {
            const KeyboardKey* key = &keys[c];
            if (key->action == KEY_GAP) continue;

            bool selected = (r == row && c == col);
            SDL_Rect rect = key_rect(&m, r, c);
            if (selected && pressed) rect = press(rect);

            // Nothing but the press changes a key's color: the shift state
            // shows in the characters the keys carry, and the lock in the LED
            uint32_t bg = THEME_COLOR3;
            if (selected) {
                bg = THEME_COLOR1;
            }
            else if (KeyboardMap_isSpecial(key)) {
                bg = mix_colors(screen, THEME_COLOR3_255, THEME_COLOR1_255,
                                KEY_BG_SPECIAL_MIX);
            }
            render_rounded_rect_bg(screen, rect.x, rect.y, rect.w, rect.h, bg);

            // The caps key carries the lock LED in its top right corner: green
            // while the shift is locked, dark otherwise. Fixed colors on
            // purpose, against the rule that colors come from the theme - an
            // LED reads as on or off only when its two states never move.
            if (key->action == KEY_CAPS) {
                int radius = rect.h * LOCK_LED_RATIO / 100;
                int led_inset = radius + SCALE1(LOCK_LED_INSET);
                uint32_t led = (shift == SHIFT_LOCKED)
                                   ? SDL_MapRGB(screen->format, 0, 220, 0)
                                   : SDL_MapRGB(screen->format, 0, 0, 0);
                fill_circle(screen, rect.x + rect.w - led_inset, rect.y + led_inset,
                            radius, led);
            }

            char buffer[KEYBOARD_LABEL_SIZE];
            TTF_Font* font = KeyboardMap_isSpecial(key) ? m.label_font : m.font;
            if (!font) font = m.font;
            draw_label(screen, font, key_label(map, other, key, shifted, font, buffer),
                       Fonts_getListTextColor(selected), rect);

            if (selected) {
                cursor_rect = rect;
                cursor_key = key;
            }
        }
    }

    if (variant >= 0 && cursor_key) {
        render_variants(screen, &m, map, cursor_key, cursor_rect, shifted, variant);
    }

    // B rubs out what has been typed, and leaves once there is nothing left
    char* b_label = (text && text[0] != '\0') ? "DELETE" : "CANCEL";

    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", b_label, "A", "TYPE", "SELECT", "DONE", NULL}, 1, screen, 1);
}
