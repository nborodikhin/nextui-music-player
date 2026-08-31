#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "display_helper.h"
#include "keyboard_map.h"
#include "utf8.h"
#include "ui_keyboard.h"
#include "ui_fonts.h"
#include "ui_utils.h"

// Space around every key, taken out of its cell, so two neighbours sit twice
// this far apart. Plus the gap under the text field, which is what sets it
// apart from the key grid.
#define KEY_PADDING 2
#define INPUT_ROW_GAP 8

// Share of a key's height the glyphs take
#define KEY_FONT_RATIO 55

// Special keys are set in small caps: uppercase, at this share of the key font
#define LABEL_FONT_RATIO 78

// Special keys color - distance from COLOR_ACCENT to COLOR_MAIN, percents
#define KEY_BG_SPECIAL_MIX 20

// The lock LED size as a percentage of the key height, and its inset from the corner
#define LOCK_LED_HEIGHT_RATIO 15
#define LOCK_LED_INSET 3

// A pressed key shrunk size, percentage of the unpressed key size
#define KEY_PRESSED_RATIO 90

// The alternates are keys like any other, on a panel that separates them from
// the grid underneath. Its padding is counted in key paddings.
#define VARIANT_PANEL_MARGIN 2


// Keyboard position, metrics and fonts.
// Keyboard consists of input field area and 5 rows of key grid
typedef struct {
    int       left;
    int       top;
    int       width;
    int       height;
    int       key_unit_size;  // one key unit, in pixels
    int       key_padding;    // spacing around each key inside its cell
    int       grid_y_offset;  // distance of the digit row from the `top`
    TTF_Font* font;           // key characters
    TTF_Font* label_font;     // the small caps on special keys
} KeyboardMetrics;

// The two fonts the keyboard draws with, opened for the key unit the screen
// gives them and kept until the display is rebuilt. Opening a font per frame
// would mean a file read and a face built on every repaint.
static TTF_Font* key_font = NULL;
static TTF_Font* label_font = NULL;   // aliases key_font when the small size fails
static int font_unit = 0;             // 0 until a size has been opened for

// One key unit for a screen of this size, the value every metric derives from
static int key_unit_for(const KeyboardGeometry* g, int width, int height) {
    int band = height - 2 * SCALE1(PADDING + PILL_SIZE);

    // The text field is one unit tall like the key rows, plus the gap under it
    int by_height = (band - SCALE1(INPUT_ROW_GAP)) / (g->rows + 1);
    int by_width = (int)(width / g->width);

    return MIN(by_height, by_width);
}

static void close_fonts(void) {
    if (label_font && label_font != key_font) TTF_CloseFont(label_font);
    if (key_font) TTF_CloseFont(key_font);

    key_font = NULL;
    label_font = NULL;
    font_unit = 0;
}

// A rebuilt display can come back a different size, so the fonts it was drawn
// with are no longer the right ones. The next frame opens them again.
static void display_recreated(void) {
    close_fonts();
}

// The label font falls back to the key font, so a special key drawn slightly
// large beats one drawn not at all. A size that would not open is not retried:
// the unit is remembered either way, and a retry per frame is a file read per
// frame.
static void open_fonts(int unit) {
    if (unit < 1) unit = 1;
    if (unit == font_unit) return;

    close_fonts();
    key_font = Fonts_open(unit * KEY_FONT_RATIO / 100);
    label_font = Fonts_open(unit * KEY_FONT_RATIO * LABEL_FONT_RATIO / 10000);
    if (!label_font) label_font = key_font;
    font_unit = unit;
}

void UIKeyboard_init(void) {
    DisplayHelper_addRecreatedCallback(display_recreated);
}

void UIKeyboard_quit(void) {
    DisplayHelper_removeRecreatedCallback(display_recreated);
    close_fonts();
}

static KeyboardMetrics compute_metrics(const KeyboardGeometry* g, SDL_Surface* screen) {
    KeyboardMetrics m;
    m.key_padding = SCALE1(KEY_PADDING);

    // The keyboard owns everything between the title pill and the button hints.
    // Both pills are PILL_SIZE tall a PADDING from their edge, so the band is
    // symmetric and a centred keyboard sits evenly inside it.
    int top = SCALE1(PADDING + PILL_SIZE);
    int bottom = screen->h - SCALE1(PADDING + PILL_SIZE);
    int available_h = bottom - top;

    // One unit drives both axes, so a height-limited board shrinks whole. Keys
    // are drawn inset inside their cells rather than spaced apart, so rows stay
    // aligned however many keys they hold.
    m.key_unit_size = key_unit_for(g, screen->w, screen->h);

    // Opened on the first frame after a display is built, not per frame
    open_fonts(m.key_unit_size);
    m.font = key_font;
    m.label_font = label_font;

    // From the top of the block, past the text field and its gap, to the digits
    m.grid_y_offset = m.key_unit_size + SCALE1(INPUT_ROW_GAP);
    m.width = (int)(g->width * m.key_unit_size);
    m.height = m.grid_y_offset + g->rows * m.key_unit_size;
    m.left = (screen->w - m.width) / 2;
    m.top = top + (available_h - m.height) / 2;

    return m;
}

// Add inner padding to the key cell to get the key rect
static SDL_Rect key_rect(const KeyboardMetrics* m, SDL_Rect key_cell) {
    return (SDL_Rect){
        .x = key_cell.x + m->key_padding,
        .y = key_cell.y + m->key_padding,
        .w = key_cell.w - 2 * m->key_padding,
        .h = key_cell.h - 2 * m->key_padding,
    };
}

// The text field: a row of its own above the grid, the full width of the board
static SDL_Rect input_rect(const KeyboardMetrics* m) {
    SDL_Rect cell = {
        .x = m->left,
        .y = m->top,
        .w = m->width,
        .h = m->key_unit_size
    };
    return key_rect(m, cell);
}

// A theme color named by its NextUI setting, for text
static SDL_Color theme_color(int color_id) {
    return uintToColour(CFG_getColor(color_id));
}

// The same, mapped for this screen's format, for fills
static uint32_t theme_color_mapped(SDL_Surface* screen, int color_id) {
    SDL_Color color = theme_color(color_id);
    return SDL_MapRGB(screen->format, color.r, color.g, color.b);
}

// Blend two mapped colors, percent of the way from the first to the second.
// Mapped pixels carry no channels of their own, so they are read back out of
// the format before the mix and mapped again after it.
static uint32_t mix_mapped_colors(SDL_Surface* screen, uint32_t a, uint32_t b, int percent) {
    Uint8 ar, ag, ab;
    Uint8 br, bg, bb;
    SDL_GetRGB(a, screen->format, &ar, &ag, &ab);
    SDL_GetRGB(b, screen->format, &br, &bg, &bb);

    return SDL_MapRGB(screen->format,
                      ar + (br - ar) * percent / 100,
                      ag + (bg - ag) * percent / 100,
                      ab + (bb - ab) * percent / 100);
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

// labels are stored as they are drawn, so only a typed character needs `out`.
static const char* key_label(const KeyboardLayout* layout, const Key* key, bool shifted) {
    if (key->label) return key->label;

    return KeyboardMap_char(layout, key, shifted, 0);
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
    render_rounded_rect_bg(screen, field->x, field->y, field->w, field->h,
                           theme_color_mapped(screen, COLOR_MAIN));
    if (!text || text[0] == '\0' || !m->font) return;

    // A long entry scrolls out to the left, so the caret end stays visible
    int text_w = 0;
    TTF_SizeUTF8(m->font, text, &text_w, NULL);
    int inner_x = field->x + SCALE1(BUTTON_PADDING);
    int inner_w = field->w - SCALE1(BUTTON_PADDING * 2);

    SDL_Surface* surface = TTF_RenderUTF8_Blended(m->font, text,
                                                  theme_color(COLOR_LIST_TEXT_SELECTED));
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
                            const KeyboardLayout* layout, const Key* key,
                            SDL_Rect anchor, bool shifted, int selected_variant) {
    int count = KeyboardMap_charCount(layout, key);
    if (count <= 0) return;

    // The strip is a keyboard row at the pressed key's scale, pitch included,
    // so one ratio governs the cells and the gaps between them
    int pitch = m->key_unit_size * KEY_PRESSED_RATIO / 100;
    int margin = m->key_padding * VARIANT_PANEL_MARGIN;
    int panel_w = pitch * count + margin * 2;
    int panel_h = pitch + margin * 2;

    // Centred on the key, then pushed back inside the screen
    int panel_x = anchor.x + anchor.w / 2 - panel_w / 2;
    if (panel_x < 0) panel_x = 0;
    if (panel_x + panel_w > screen->w) panel_x = screen->w - panel_w;

    // Above the key, or below it when there is no room over the top row
    int panel_y = anchor.y - margin - panel_h;
    if (panel_y < m->top) panel_y = anchor.y + anchor.h + margin;

    // Variant chars are drawn on a panel colored as pressed key
    int panel_bg_color_id = COLOR_MAIN;
    int variant_color_id = panel_bg_color_id;
    int variant_text_color_id = COLOR_LIST_TEXT_SELECTED;
    int selected_variant_color_id = COLOR_ACCENT2;
    int selected_variant_text_color_id = COLOR_LIST_TEXT;

    render_rounded_rect_bg(screen, panel_x, panel_y, panel_w, panel_h,
                           theme_color_mapped(screen, panel_bg_color_id));

    for (int index = 0; index < count; index++) {
        SDL_Rect cell = {
            .x = panel_x + margin + index * pitch,
            .y = panel_y + margin,
            .w = pitch,
            .h = pitch,
        };
        SDL_Rect rect = key_rect(m, cell);

        bool selected = (index == selected_variant);
        int color_id_bg = selected ? selected_variant_color_id : variant_color_id;
        int color_id_label = selected ? selected_variant_text_color_id : variant_text_color_id;

        render_rounded_rect_bg(screen, rect.x, rect.y, rect.w, rect.h,
                               theme_color_mapped(screen, color_id_bg));
        draw_label(screen, m->font, KeyboardMap_char(layout, key, shifted, index),
                   theme_color(color_id_label),
                   rect);
    }
}

bool UIKeyboard_stateEquals(const KeyboardUiState* a, const KeyboardUiState* b) {
    // Bytewise, so a field added to the state is covered without a line here
    return memcmp(a, b, sizeof *a) == 0;
}

void UIKeyboard_render(SDL_Surface* screen, const KeyboardUiState* state) {
    const char* text = state->text;

    GFX_clear(screen);
    render_screen_header(screen, state->title ? state->title : "", state->show_setting);

    const KeyboardLayout* layout = state->layout;
    bool shifted = (state->shift != SHIFT_OFF);
    KeyboardMetrics m = compute_metrics(layout->geometry, screen);

    SDL_Rect field = input_rect(&m);
    render_input(screen, &m, &field, text);

    SDL_Rect cursor_rect = {0, 0, 0, 0};
    const Key* cursor_key = NULL;

    for (int row = 0; layout->geometry->keys[row]; row++) {
        const Key* const* keys = layout->geometry->keys[row];

        for (int col = 0; keys[col]; col++) {
            const Key* key = keys[col];
            if (key->action == KEY_SPACER) continue;

            // The lang key names where it takes you, not the current layout
            const char* label = (key->action != KEY_LANG)
                                    ? key_label(layout, key, shifted)
                                    : state->lang_label;

            // Both edges are narrowed, not the width, so a key's right edge
            // is exactly the next one's left edge and the row ends on m.width
            int left_px = (int)(key->left * m.key_unit_size);
            int right_px = (int)((key->left + key->width) * m.key_unit_size);

            const SDL_Rect key_cell = (SDL_Rect){
                .x = m.left + left_px,
                .y = m.top + m.grid_y_offset + row * m.key_unit_size,
                .w = right_px - left_px,
                .h = m.key_unit_size
            };

            SDL_Rect rect = key_rect(&m, key_cell);
            bool selected = (row == state->row && col == state->col);
            if (selected && state->pressed) rect = press(rect);

            // Nothing but the press changes a key's color: the shift state
            // shows in the characters the keys carry, and the lock in the LED
            uint32_t regular_bg = theme_color_mapped(screen, COLOR_ACCENT2);
            uint32_t selected_bg = theme_color_mapped(screen, COLOR_MAIN);

            uint32_t bg = regular_bg;
            if (selected) {
                bg = selected_bg;
            } else if (key->action != KEY_TEXT) {
                bg = mix_mapped_colors(screen, regular_bg, selected_bg, KEY_BG_SPECIAL_MIX);
            }
            render_rounded_rect_bg(screen, rect.x, rect.y, rect.w, rect.h, bg);

            // The caps key carries the lock LED in its top right corner: green
            // while the shift is locked, dark otherwise. Fixed colors on
            // purpose, against the rule that colors come from the theme - an
            // LED reads as on or off only when its two states never move.
            if (key->action == KEY_CAPS) {
                int radius = rect.h * LOCK_LED_HEIGHT_RATIO / 100;
                int led_inset = radius + SCALE1(LOCK_LED_INSET);
                uint32_t led = (state->shift == SHIFT_LOCKED)
                                   ? SDL_MapRGB(screen->format, 0, 220, 0)
                                   : SDL_MapRGB(screen->format, 0, 0, 0);
                fill_circle(screen, rect.x + rect.w - led_inset, rect.y + led_inset,
                            radius, led);
            }

            TTF_Font* font = key->action != KEY_TEXT ? m.label_font : m.font;

            int label_color_id = selected ? COLOR_LIST_TEXT_SELECTED : COLOR_LIST_TEXT;
            draw_label(screen, font, label, theme_color(label_color_id), rect);

            if (selected) {
                cursor_rect = rect;
                cursor_key = key;
            }
        }
    }

    if (state->picking_variant && cursor_key) {
        render_variants(screen, &m, layout, cursor_key, cursor_rect, shifted,
                        state->current_variant);
    }

    // B rubs out what has been typed, and leaves once there is nothing left
    char* b_label = (text && text[0] != '\0') ? "DELETE" : "CANCEL";

    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", b_label, "A", "TYPE", "SELECT", "DONE", NULL}, 1, screen, 1);
}
