#ifndef __UI_UTILS_H__
#define __UI_UTILS_H__

#include <stdbool.h>
#include <stdint.h>
#include "defines.h"  // Brings in SDL2 via platform.h -> sdl.h
#include "api.h"      // For SDL types and TTF
#include "player.h"   // For AudioFormat
#include "ui_theme.h"
#include "screen_title.h"

// Format duration as MM:SS
void format_time(char* buf, int ms);

// Get format name string
const char* get_format_name(AudioFormat format);
// Scrolling text state for marquee animation
typedef struct {
    char text[512];         // Text to display
    int text_width;         // Full text width in pixels
    int max_width;          // Maximum display width
    uint32_t start_time;    // Animation start time
    bool needs_scroll;      // True if text is wider than max_width
    int scroll_offset;      // Current pixel offset for smooth scrolling
    bool use_gpu_scroll;    // True = use GPU layer (for lists), False = software (for player)
    int last_x, last_y;     // Last render position (for animate-only mode)
    TTF_Font* last_font;    // Last font used (for animate-only mode)
    SDL_Color last_color;   // Last color used (for animate-only mode)
    ThemeRole role;         // Role of the cached text
    bool selected;          // True where the text sits on a selection pill
    SDL_Surface* cached_scroll_surface;  // Cached surface for GPU scroll (no bg)
    bool scroll_active;     // True once GPU scroll has actually started (after delay)
} ScrollTextState;

// Reset scroll state for new text
// use_gpu: true for lists (GPU layer with pill bg), false for player (software, no bg)
void ScrollText_reset(ScrollTextState* state, const char* text, TTF_Font* font,
                      int max_width, ThemeRole role, bool selected, bool use_gpu);

// Check if scrolling is active (text needs to scroll)
bool ScrollText_isScrolling(ScrollTextState* state);

// Check if scroll needs a render to transition from delay to active
bool ScrollText_needsRender(ScrollTextState* state);

// Activate scrolling after delay (for player screens that bypass ScrollText_render)
void ScrollText_activateAfterDelay(ScrollTextState* state);

// Update scroll animation only (for GPU mode, doesn't redraw screen)
// Call this when dirty=0 but scrolling is active - uses saved position from last render
void ScrollText_animateOnly(ScrollTextState* state);

// Render scrolling text (call every frame)
void ScrollText_render(ScrollTextState* state, TTF_Font* font, SDL_Color color,
                       SDL_Surface* screen, int x, int y);

// Unified update: checks for text change, resets if needed, and renders
// use_gpu: true for lists (GPU layer with pill bg), false for player (software, no bg)
void ScrollText_update(ScrollTextState* state, const char* text, TTF_Font* font,
                       int max_width, ThemeRole role, bool selected, SDL_Surface* screen,
                       int x, int y, bool use_gpu);

// Draw the scrolling text at its current offset onto `layer`, and advance it. Does not
// clear the layer or flip: the caller owns the layer and decides what else goes
// on it.
void ScrollText_paintGPU(ScrollTextState* state, TTF_Font* font,
                         SDL_Color color, int x, int y, int layer);

// The y of the top of the chip of a playing screen. The chip shares the line of the
// status group, thus it takes the top margin where the platform draws none. It then
// keeps the same gap to the top edge as to the left edge.
int top_of_the_chip_box(SDL_Surface* screen, int chip_h);

// The height of the header of a screen (the top pill row) including all
// applicable paddings. This is the top boundary content may use.
// If status group is not displayed, the height could be defined by `chip_h`.
int total_header_height(SDL_Surface* screen, int chip_h);


// The total height of the bottom chip footer of a screen including
// all applicable paddings, used when pills are not used (e.g. playing screen).
int chip_footer_height(int row_h);

// The y of the top of a box `box_h` high, which takes the role of the bottom
// pill row on the screen that draws no button hint (e.g. playing screen).
int top_of_the_footer_chip_box(SDL_Surface* screen, int box_h);

// True where the screen is wide enough for the platform to draw the status group.
bool screen_has_status_group(SDL_Surface* screen);

// The area of the screen title on a screen whose status group is `status_w` wide.
// Pass 0 in `status_w` where the platform draws no status group. The area ends one
// inset before the status group, thus a title never runs under it.
SDL_Rect screen_title_area(SDL_Surface* screen, int status_w, int text_h);

// Paints `title` at rest in `area` of the surface and returns true. A title that
// moves is not painted here, and false comes back: its marquee goes on
// LAYER_SCROLLTEXT, thus a frame of the marquee costs no redraw of the surface.
bool paint_screen_title(SDL_Surface* screen, ScreenTitle* title, TTF_Font* font,
                        SDL_Color color, SDL_Rect area, uint32_t now);

// Starts the screen title of a module, on its entry. Pass true in `defer` to
// let a change of text scroll in while the title moves, as the file browser
// does; with false a change replaces the text at once. Returns the mode of the
// title before, thus a nested loop such as the keyboard starts the outer title
// again with that mode when it returns.
bool ScreenTitle_start(bool defer);

// Call at the start of each frame, before any paint of LAYER_SCROLLTEXT.
void ScreenTitle_frameBegin(void);

// Call once at the end of each frame of a module loop, after its flip. Pass true
// in `has_header` where the screen of this frame draws a standard header. Sets
// `dirty` where the title needs a frame of the surface, and paints the marquee
// on LAYER_SCROLLTEXT where the row of the list did not paint the layer this
// frame. A screen with no header leaves the layer to its own painter.
void ScreenTitle_frameEnd(int* dirty, bool has_header);

// Draws the status group and the screen title `text`. The title of a module is
// one state that ScreenTitle_start() begins.
void render_screen_header(SDL_Surface* screen, const char* text, int show_setting);

// Adjust scroll offset to keep selected item visible
void adjust_list_scroll(int selected, int* scroll, int items_per_page);

// Page toward the beginning of the list (lower indices), adusting scroll position and selection accordingly.
// Returns true if selection or scroll changed.
// Note: non-positive items_per_page is treated as 1.
bool list_page_up(int *selected, int *scroll, int total_count, int items_per_page);

// Page toward the end of the list (higher indices), adusting scroll position and selection accordingly.
// Returns true if selection or scroll changed
// Note: non-positive items_per_page is treated as 1.
bool list_page_down(int *selected, int *scroll, int total_count, int items_per_page);

// Draws one scroll indicator, `ASSET_SCROLL_UP` or `ASSET_SCROLL_DOWN`, in the
// color of the unselected secondary role.
void draw_scroll_indicator(SDL_Surface* screen, int asset, int x, int y);


// ============================================
// Generic List Rendering Helpers
// ============================================

// Layout information for a standard scrollable list
typedef struct {
    int list_y;          // Y position where list starts
    int list_h;          // Total height available for list
    int item_h;          // Height of each item
    int items_per_page;  // Number of visible items
    int rich_item_h;         // Height of a "rich" row (1.5x), used by podcast/downloader
    int rich_items_per_page; // Number of visible rich rows
    int max_width;       // Maximum width for content (hw - padding*2)
} ListLayout;

// Calculate standard list layout based on screen dimensions
ListLayout calc_list_layout(SDL_Surface* screen);

// The height of a scroll indicator. A list keeps this room under its last row.
int scroll_indicator_height(void);

// The x of a scroll indicator, centered on the screen.
int scroll_indicator_x(SDL_Surface* screen);

// Draws the up indicator above the first row where rows are above the page, and
// the down indicator directly under the last row of the page where rows are below
// it. Pass the layout of the rows, thus a rich list gives its own row height.
void render_scroll_indicators(SDL_Surface* screen, const ListLayout* layout, int scroll,
                              int total_count);

// The same two indicators for a stream that scrolls by the pixel through the
// viewport of `layout`: `scroll` is the content pixel at the top of the viewport
// and `content_h` the height of the whole stream.
void render_stream_scroll_indicators(SDL_Surface* screen, const ListLayout* layout, int scroll,
                                     int content_h);

// Render a list item's text with optional scrolling for selected items
// Returns the text_x position after any prefix (useful for chaining)
// If scroll_state is NULL, no scrolling is used
void render_list_item_text(SDL_Surface* screen, ScrollTextState* scroll_state,
                           const char* text, TTF_Font* font_param,
                           int text_x, int text_y, int max_text_width,
                           bool selected);

// Position information returned by render_list_item_pill
typedef struct {
    int pill_width;   // Width of the rendered pill
    int text_x;       // X position for text (after padding)
    int text_y;       // Y position for text (vertically centered)
} ListItemPos;

// The height of a chip. A caller needs it before it draws, to put the chip on the
// middle of the top pill row.
int chip_height(void);

// Render a list item's pill background and calculate text position
// Combines: the row width + draw_list_item_bg + text position calculation
// prefix_width: extra width to account for (e.g., checkbox, indicator)
// A chip: a short label in a rectangular outline with no fill, which names the
// source of what plays. Gives the rectangle that it drew.
SDL_Rect draw_chip(SDL_Surface* screen, const char* text, int x, int y);

// The band behind a whole row, for a row that carries a label at its right edge.
void draw_list_item_band(SDL_Surface* screen, SDL_Rect* rect);

// The pill behind the row that the cursor is on. Draws nothing for another row.
void draw_list_item_bg(SDL_Surface* screen, SDL_Rect* rect, bool selected);

// A row whose right edge carries a label gives the width of that label, thus
// the title truncates before it and the two never overlap. Give 0 where a row
// has no such label.
//
// Such a row draws in four steps: the band of draw_list_item_band(), the
// label at the right edge, this pill, and then the title. The pill thus covers
// the label if it ever reaches it, and the title covers nothing else.
ListItemPos render_list_item_pill_right(SDL_Surface* screen, ListLayout* layout,
                                        const char* text, char* truncated,
                                        int y, bool selected, int prefix_width,
                                        int right_width);

ListItemPos render_list_item_pill(SDL_Surface* screen, ListLayout* layout,
                                   const char* text, char* truncated,
                                   int y, bool selected, int prefix_width);

// Position information returned by render_list_item_pill_badged
typedef struct {
    int pill_width;     // Width of the title (inner) pill
    int text_x;         // X position for title text (after padding)
    int text_y;         // Y position for title text (medium font, row 1)
    int subtitle_x;     // X position for subtitle text (row 2)
    int subtitle_y;     // Y position for subtitle text (small font, row 2)
    int badge_x;        // X position for badge content start
    int badge_y;        // Y position for badge content (tiny font, centered)
    int total_width;    // Total width of title pill + badge area
    int text_max_width; // Max width for text content (accounts for capsule radius)
} ListItemBadgedPos;

// Render a list item's pill with optional right-side badge area (settings-style two-layer)
// When badge_width > 0 and selected: the band + the selection pill around the title
// When badge_width == 0: behaves like render_list_item_pill
// Pill width considers both title and subtitle (whichever is wider)
// subtitle can be NULL if not needed for pill sizing
// extra_subtitle_width: additional width to add to subtitle measurement (e.g. progress bar)
// Caller renders badge content at badge_x, badge_y
ListItemBadgedPos render_list_item_pill_badged(SDL_Surface* screen, ListLayout* layout,
                                                const char* text, const char* subtitle,
                                                char* truncated,
                                                int y, bool selected, int badge_width,
                                                int extra_subtitle_width);

// Position information returned by render_list_item_pill_rich
typedef struct {
    int pill_width;              // Width of the rendered pill
    int title_x, title_y;       // Row 1 position (medium font)
    int subtitle_x, subtitle_y; // Row 2 position (small font)
    int image_x, image_y;       // Image position (top-left corner)
    int image_size;              // Image width & height (square)
    int text_max_width;          // Max width for text (for scrolling)
} ListItemRichPos;

// Render a 2-row list item pill with image area on the left
// Height is 1.5x PILL_SIZE. Image is square, vertically centered.
// Row 1: title (medium font), Row 2: subtitle (small font)
// Caller renders image at image_x/image_y and text via render_list_item_text()
ListItemRichPos render_list_item_pill_rich(SDL_Surface* screen, ListLayout* layout,
                                            const char* title, const char* subtitle,
                                            char* truncated,
                                            int y, bool selected, bool has_image,
                                            int extra_subtitle_width);

// Position information returned by render_menu_item_pill
typedef struct {
    int pill_width;   // Width of the rendered pill
    int text_x;       // X position for text (after padding)
    int text_y;       // Y position for text (vertically centered in pill)
    int item_y;       // Y position of this menu item
} MenuItemPos;

// Render a menu item's pill background and calculate text position
// Menu items have small spacing (2px) between them (item_h includes margin, pill uses PILL_SIZE)
// index: menu item index (0-based)
// prefix_width: extra width to account for (e.g., icon)
MenuItemPos render_menu_item_pill(SDL_Surface* screen, ListLayout* layout,
                                   const char* text, char* truncated,
                                   int index, bool selected, int prefix_width);

// ============================================
// Generic Simple Menu Rendering
// ============================================

// Callback to customize item label (e.g., "About" -> "About (Update Available)")
// Returns custom label or NULL to use default
typedef const char* (*MenuItemLabelCallback)(int index, const char* default_label,
                                              char* buffer, int buffer_size);

// Callback to render right-side badge (e.g., queue count)
// Called after pill is rendered, can draw additional elements
typedef void (*MenuItemBadgeCallback)(SDL_Surface* screen, int index, bool selected,
                                       int item_y, int item_h);

// Callback to get icon for a menu item
// Returns SDL_Surface* icon or NULL if no icon for this item
typedef SDL_Surface* (*MenuItemIconCallback)(int index, bool selected);

// Callback for custom text rendering (e.g., fixed prefix + scrolling suffix)
// Return true if custom rendering was handled, false to use default
typedef bool (*MenuItemCustomTextCallback)(SDL_Surface* screen, int index, bool selected,
                                            int text_x, int text_y, int max_text_width);

// Configuration for generic simple menu rendering
typedef struct {
    const char* title;                    // Header title
    const char** items;                   // Array of menu item labels
    int item_count;                       // Number of items
    const char* btn_b_label;              // B button label ("EXIT", "BACK", etc.)
    MenuItemLabelCallback get_label;      // Optional: customize item label
    MenuItemBadgeCallback render_badge;   // Optional: render right-side badge
    MenuItemIconCallback get_icon;        // Optional: get icon for item
    MenuItemCustomTextCallback render_text; // Optional: custom text rendering
} SimpleMenuConfig;

// Render a simple menu with optional customization callbacks.
// `menu_scroll` is the index of the first visible row; menus shorter than a
// page pass 0 and the window logic is inert.
void render_simple_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                        int menu_scroll, const SimpleMenuConfig* config);

// ============================================
// Rounded Rectangle Background
// ============================================

// Render a filled rounded rectangle background
// Works at any height (unlike pill asset which requires PILL_SIZE)
// Uses two overlapping rects to create corner inset effect
void render_rounded_rect_bg(SDL_Surface* screen, int x, int y, int w, int h, uint32_t color);

// ============================================
// Dialog Box
// ============================================

// Layout information returned by render_dialog_box
typedef struct {
    int box_x, box_y;    // Top-left corner of the box
    int box_w, box_h;    // Box dimensions
    int content_x;       // Left margin for content
    int content_w;       // Width available for content
} DialogBox;

// Render a dialog box centered on screen with white border
// Clears GPU scroll text layer + fills entire screen black + draws box with border
// Returns box dimensions for the caller to render content inside
DialogBox render_dialog_box(SDL_Surface* screen, int box_w, int box_h);

// ============================================
// Empty State
// ============================================

// Render centered empty state with icon, message, optional subtitle, and button groups
// Renders the empty icon + message (medium/white) + subtitle (small/gray)
// y_button_label: label for Y button (e.g., "NEW", "MANAGE"), or NULL for no Y button
void render_empty_state(SDL_Surface* screen, const char* message,
                        const char* subtitle, const char* y_button_label);

#endif
