#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ui_utils.h"

// The form of a chip: the space around its label, and its height with no label.
#define CHIP_PADDING_X 5
#define CHIP_PADDING_Y 2
#define CHIP_MIN_HEIGHT 16
#include "ui_fonts.h"
#include "ui_icons.h"
#include "module_common.h"
#include "ui_layers.h"
#include "list_nav.h"

// Format duration as MM:SS
void format_time(char* buf, int ms) {
    int total_secs = ms / 1000;
    int mins = total_secs / 60;
    int secs = total_secs % 60;
    sprintf(buf, "%02d:%02d", mins, secs);
}

// Get format name string
const char* get_format_name(AudioFormat format) {
    switch (format) {
        case AUDIO_FORMAT_MP3: return "MP3";
        case AUDIO_FORMAT_FLAC: return "FLAC";
        case AUDIO_FORMAT_OGG: return "OGG";
        case AUDIO_FORMAT_WAV: return "WAV";
        case AUDIO_FORMAT_MOD: return "MOD";
        case AUDIO_FORMAT_M4A: return "M4A";
        case AUDIO_FORMAT_AAC: return "AAC";
        case AUDIO_FORMAT_OPUS: return "OPUS";
        default: return "---";
    }
}

// Scroll gap for software scrolling
#define SCROLL_GAP 30

// Delay before scrolling starts (ms) - show static text first
#define SCROLL_START_DELAY 1000

// Reset scroll state for new text
void ScrollText_reset(ScrollTextState* state, const char* text, TTF_Font* font,
                      int max_width, ThemeRole role, bool selected, bool use_gpu) {
    // The marquee of the old text leaves the layer with it
    UiLayer_clear(UI_LAYER_ANIMATION);

    // Free old cached surface if exists
    if (state->cached_scroll_surface) {
        SDL_FreeSurface(state->cached_scroll_surface);
        state->cached_scroll_surface = NULL;
    }

    strncpy(state->text, text, sizeof(state->text) - 1);
    state->text[sizeof(state->text) - 1] = '\0';
    int text_h = 0;
    TTF_SizeUTF8(font, state->text, &state->text_width, &text_h);
    state->max_width = max_width;
    state->role = role;
    state->selected = selected;
    state->start_time = SDL_GetTicks();
    state->scroll_offset = 0;
    state->use_gpu_scroll = use_gpu;

    // Don't enable scrolling yet - delay it so text appears static first
    // needs_scroll will be set to true in ScrollText_render after SCROLL_START_DELAY
    state->needs_scroll = false;

    // Pre-create cached scroll surface for GPU scroll without background
    if ((state->text_width > max_width) && use_gpu) {
        int padding = SCALE1(SCROLL_GAP);
        int total_width = state->text_width * 2 + padding;
        int height = TTF_FontHeight(font);

        state->cached_scroll_surface = SDL_CreateRGBSurfaceWithFormat(0,
            total_width, height, 32, SDL_PIXELFORMAT_ARGB8888);

        if (state->cached_scroll_surface) {
            // Clear to transparent
            SDL_SetSurfaceBlendMode(state->cached_scroll_surface, SDL_BLENDMODE_NONE);
            SDL_FillRect(state->cached_scroll_surface, NULL, 0);

            // Render text twice for seamless looping
            SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font, state->text,
                                                            Theme_getColor(role, state->selected));
            if (text_surf) {
                // Important: TTF rendering produces "transparent white" pixels which are not handled well by
                // the GPU layer which expects premultiplied pixels.
                // BLEND mode converts those into "transparent black"
                SDL_SetSurfaceBlendMode(text_surf, SDL_BLENDMODE_BLEND);
                SDL_BlitSurface(text_surf, NULL, state->cached_scroll_surface, &(SDL_Rect){0, 0, 0, 0});
                SDL_BlitSurface(text_surf, NULL, state->cached_scroll_surface, &(SDL_Rect){state->text_width + padding, 0, 0, 0});
                SDL_FreeSurface(text_surf);
            }
        }
    }
}

void ScrollText_forget(ScrollTextState* state) {
    if (state->cached_scroll_surface) SDL_FreeSurface(state->cached_scroll_surface);
    memset(state, 0, sizeof(*state));
}

// Check if scrolling is active (text needs to scroll)
bool ScrollText_isScrolling(ScrollTextState* state) {
    return state->needs_scroll;
}

// True on the frame that ends the delay of a text that is too wide: the render
// of that frame starts the marquee. The delay itself asks for no render, thus
// a list does not redraw on each frame for one second while the title marquee
// moves on the layer.
bool ScrollText_needsRender(ScrollTextState* state) {
    return state->text[0] && state->text_width > state->max_width && !state->needs_scroll &&
           SDL_GetTicks() - state->start_time >= SCROLL_START_DELAY;
}

// Activate scrolling after delay (for player screens that bypass ScrollText_render)
void ScrollText_activateAfterDelay(ScrollTextState* state) {
    if (!state->needs_scroll && state->text_width > state->max_width &&
        SDL_GetTicks() - state->start_time >= SCROLL_START_DELAY) {
        state->needs_scroll = true;
    }
}

// The screen title. One standard header is on the screen at a time, thus one
// state serves each module: a module starts it on entry and the header renders
// through it. A title that moves is on the animation layer with the marquee of
// the selected row, thus the two share one clear a frame. The row paints the
// layer where it moves, and it puts the title on the layer before the marquee.
static ScreenTitle title_state;
static bool        header_drawn = false;   // the last render drew a standard header
static SDL_Rect    header_area;
static bool        layer_painted_this_frame = false;  // by a row or the title
static bool        layer_holds_title = false;
static bool        title_on_surface = false;  // the last render drew the title at rest

static void paint_title_on_layer(uint32_t now);

// Draws the marquee of `state` at its offset on the animation layer, and moves
// it `step` pixels for the next frame. Draws nothing where the text fits.
static void paint_cached_marquee(ScrollTextState* state, int x, int y, int step) {
    if (!state->text[0] || !state->needs_scroll || !state->cached_scroll_surface) return;

    int padding = SCALE1(SCROLL_GAP);
    int height = state->cached_scroll_surface->h;

    // Create clipped view at current scroll offset (only this is created per-frame)
    SDL_Surface* clipped = SDL_CreateRGBSurfaceWithFormat(0,
        state->max_width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!clipped) return;

    SDL_SetSurfaceBlendMode(state->cached_scroll_surface, SDL_BLENDMODE_NONE);
    SDL_FillRect(clipped, NULL, 0);
    SDL_Rect src = {state->scroll_offset, 0, state->max_width, height};
    SDL_BlitSurface(state->cached_scroll_surface, &src, clipped, NULL);

    UiLayer_blit(clipped, x, y, UI_LAYER_ANIMATION);
    SDL_FreeSurface(clipped);

    state->scroll_offset += step;
    if (state->scroll_offset >= state->text_width + padding) {
        state->scroll_offset = 0;
    }
}

// The marquee of a row moves two pixels a frame, as the rows of the platform do.
#define ROW_MARQUEE_STEP 2

// Paints the animation layer of a list screen: the title of the header where it
// is not on the surface, then the marquee of the row. One clear serves both.
static void paint_row_layer(ScrollTextState* state, int x, int y) {
    UiLayer_clear(UI_LAYER_ANIMATION);
    layer_holds_title = false;
    paint_title_on_layer(SDL_GetTicks());
    layer_painted_this_frame = true;
    paint_cached_marquee(state, x, y, ROW_MARQUEE_STEP);
}

// Update scroll animation only (for GPU mode, doesn't redraw screen)
// Call this when dirty=0 but scrolling is active - uses saved position from last render
void ScrollText_animateOnly(ScrollTextState* state) {
    if (!state->text[0] || !state->needs_scroll || !state->use_gpu_scroll) return;
    if (!state->last_font) return;  // Never rendered yet

    paint_row_layer(state, state->last_x, state->last_y);
}

void ScrollText_render(ScrollTextState* state, TTF_Font* font, SDL_Color color,
                       SDL_Surface* screen, int x, int y) {
    if (!state->text[0]) return;

    // Save position info for animate-only mode
    state->last_x = x;
    state->last_y = y;
    state->last_font = font;
    state->last_color = color;

    // Check if scroll delay has elapsed - activate scrolling
    ScrollText_activateAfterDelay(state);

    // If text fits (or still in delay/transition), render normally without scrolling
    if (!state->needs_scroll) {
        // The layer holds the marquee of the row before, thus it clears. The
        // title that moves goes back on it.
        paint_row_layer(state, x, y);
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, state->text, color);
        if (surf) {
            SDL_Rect src = {0, 0, surf->w > state->max_width ? state->max_width : surf->w, surf->h};
            SDL_BlitSurface(surf, &src, screen, &(SDL_Rect){x, y, 0, 0});
            SDL_FreeSurface(surf);
        }
        return;
    }

    // The marquee is on the layer, thus the surface holds no text under it
    paint_row_layer(state, x, y);
}

// Unified update: checks for text change, resets if needed, and renders
void ScrollText_update(ScrollTextState* state, const char* text, TTF_Font* font,
                       int max_width, ThemeRole role, bool selected, SDL_Surface* screen,
                       int x, int y, bool use_gpu) {
    // Rebuild the cache when its text, its role or the state of its row changes.
    // ScrollText_reset() bakes the color into the cached surface.
    if (strcmp(state->text, text) != 0 || state->role != role
        || state->selected != selected) {
        ScrollText_reset(state, text, font, max_width, role, selected, use_gpu);
    }
    ScrollText_render(state, font, Theme_getColor(role, selected), screen, x, y);
}

// The marquee of a playing title, on the layer of the playing screen. It moves
// one pixel a frame, thus a long title reads at rest.
void ScrollText_paintGPU(ScrollTextState* state, TTF_Font* font,
                         SDL_Color color, int x, int y) {
    // Save render info
    state->last_x = x;
    state->last_y = y;
    state->last_font = font;
    state->last_color = color;

    paint_cached_marquee(state, x, y, 1);
}

// The platform draws the status group only where the screen has the room for it.
bool screen_has_status_group(SDL_Surface* screen) {
    return screen->w >= SCALE1(320);
}

// The y of the top of a box `box_h` high, centered on the top pill row, which is the
// row that the platform keeps for the status group. The row is there whether the
// platform fills it or not, thus a screen title keeps the height of a selected row on
// each screen, as the menu of the platform does.
static int top_of_the_pill_row_box(int box_h, bool use_status_group) {
    return use_status_group ? SCALE1(PADDING) + (SCALE1(PILL_SIZE) - box_h) / 2
                            : SCALE1(PADDING);
}

// The chip of a playing screen shares the line of the status group. Where the
// platform draws no status group, the chip takes the top margin, thus its gap to
// the top edge and its gap to the left edge are the same.
int top_of_the_chip_box(SDL_Surface* screen, int chip_h) {
    return top_of_the_pill_row_box(chip_h, screen_has_status_group(screen));
}

int total_header_height(SDL_Surface* screen, int chip_h) {
    // The pill row holds its own room around the pill, thus the content that follows
    // needs no gap of its own. A chip is snug, thus it takes a margin below it.
    return screen_has_status_group(screen) ? SCALE1(PADDING + PILL_SIZE)
                                           : SCALE1(PADDING) + chip_h + SCALE1(PADDING);
}

int chip_footer_height(int row_h) {
    return SCALE1(PADDING) + row_h + SCALE1(PADDING);
}

int top_of_the_footer_chip_box(SDL_Surface* screen, int box_h) {
    return screen->h - SCALE1(PADDING) - box_h;
}

SDL_Rect screen_title_area(SDL_Surface* screen, int status_w, int text_h) {
    // The same inset on each side, thus the title and the status group keep one gap.
    int x     = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    int right = screen->w - SCALE1(PADDING) - SCALE1(BUTTON_PADDING) - status_w;
    // The title keeps a row as high as a selection pill, with the status group
    // or without it, thus the list below it starts on the same line either way.
    return (SDL_Rect){x, top_of_the_pill_row_box(text_h, true), right - x, text_h};
}

// A rendered text of the tape. A text is immutable while it is on the tape,
// thus each renders once, with its wash baked in, and each frame blits it.
typedef struct {
    char         text[SCREEN_TITLE_MAX];
    int          len;
    bool         cut;
    SDL_Color    color;
    SDL_Surface* surf;
} SegmentCache;
static SegmentCache segment_cache[SCREEN_TITLE_SEGMENTS];

static bool same_color(SDL_Color a, SDL_Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Returns the rendered text of `seg`, from the cache of slot `slot` where it
// holds that text already. A cut text washes out over the last `fade_w` of its
// room: its alpha goes from opaque to transparent, and the text ends there.
static SDL_Surface* segment_surface(int slot, const ScreenTitleSegment* seg, TTF_Font* font,
                                    SDL_Color color, int fade_w) {
    SegmentCache* c = &segment_cache[slot];
    if (c->surf && c->len == seg->len && c->cut == seg->cut && same_color(c->color, color)
        && strcmp(c->text, seg->text) == 0) {
        return c->surf;
    }
    if (c->surf) SDL_FreeSurface(c->surf);
    c->surf = TTF_RenderUTF8_Blended(font, seg->text, color);
    if (!c->surf) return NULL;
    snprintf(c->text, sizeof(c->text), "%s", seg->text);
    c->len   = seg->len;
    c->cut   = seg->cut;
    c->color = color;

    if (seg->cut) {
        SDL_Surface* surf = c->surf;
        int fade_x = seg->len - fade_w;
        // The text surface of SDL_ttf is ARGB8888, thus the alpha is the top byte
        uint32_t* px = surf->pixels;
        int pitch = surf->pitch / 4;
        for (int col = fade_x; col < seg->len && col < surf->w; col++) {
            uint32_t keep = (uint32_t)(255 * (seg->len - col) / fade_w);
            for (int row = 0; row < surf->h; row++) {
                uint32_t* p = &px[row * pitch + col];
                uint32_t a = (*p >> 24) * keep / 255;
                *p = (*p & 0x00ffffffu) | (a << 24);
            }
        }
    }
    return c->surf;
}

static void blit_segment(SDL_Surface* dst, int x, int y, int slot, const ScreenTitleSegment* seg,
                         TTF_Font* font, SDL_Color color, int fade_w) {
    SDL_Surface* surf = segment_surface(slot, seg, font, color, fade_w);
    if (!surf) return;
    SDL_Rect src = {0, 0, seg->cut ? seg->len : surf->w, surf->h};
    SDL_BlitSurface(surf, &src, dst, &(SDL_Rect){x, y});
}

// Blits the slice of the tape that the area shows onto `dst`, with the area at
// `x`, `y` of `dst`. Each text renders whole and the clip shows the slice, thus
// no copy of a text has a size limit of its own.
static void blit_title_slice(SDL_Surface* dst, int x, int y, ScreenTitle* title,
                             TTF_Font* font, SDL_Color color, int area_w, uint32_t now) {
    int em  = TTF_FontHeight(font);
    int gap = title->gap;
    int offset = ScreenTitle_offset(title, now);
    if (title->count == 0) return;

    SDL_Rect clip;
    SDL_GetClipRect(dst, &clip);
    SDL_SetClipRect(dst, &(SDL_Rect){x, y, area_w, em});

    int pos = x - offset;
    for (int i = 0; i < title->count && pos < x + area_w; i++) {
        blit_segment(dst, pos, y, i, &title->seg[i], font, color, em);
        pos += title->seg[i].len + gap;
    }
    if (title->count == 1 && offset > 0 && pos < x + area_w) {
        // The one text loops: the next copy follows the gap, thus the slice
        // never shows an end
        blit_segment(dst, pos, y, 0, &title->seg[0], font, color, em);
    }
    SDL_SetClipRect(dst, &clip);
}

bool paint_screen_title(SDL_Surface* screen, ScreenTitle* title, TTF_Font* font,
                        SDL_Color color, SDL_Rect area, uint32_t now) {
    // One em between two texts, and one em of wash at a cut: the gap of a row
    // marquee reads as a hole in a title. The area is the one of this frame: the
    // status group changes width with a setting pill, and a title that fit can
    // then move, or the reverse.
    int em = TTF_FontHeight(font);
    ScreenTitle_measure(title, area.w, em, em, now);
    if (ScreenTitle_moves(title, now)) {
        // The marquee is on the GPU layer, thus the surface holds no title under it.
        return false;
    }
    blit_title_slice(screen, area.x, area.y, title, font, color, area.w, now);
    return true;
}

// Paints the title on the animation layer, after a clear of the layer. The
// layer shows the title while the surface does not: while it moves.
static void paint_title_on_layer(uint32_t now) {
    if (!header_drawn || title_on_surface) return;

    TTF_Font* font = Fonts_getLarge();
    SDL_Surface* slice = SDL_CreateRGBSurfaceWithFormat(0, header_area.w, header_area.h, 32,
                                                        SDL_PIXELFORMAT_ARGB8888);
    if (!slice) return;
    SDL_FillRect(slice, NULL, 0);
    blit_title_slice(slice, 0, 0, &title_state, font,
                     Theme_getColor(THEME_ROLE_SECONDARY, false), header_area.w, now);
    UiLayer_blit(slice, header_area.x, header_area.y, UI_LAYER_ANIMATION);
    SDL_FreeSurface(slice);
    layer_holds_title = true;
}

bool ScreenTitle_start(bool defer) {
    bool before = title_state.defer;
    ScreenTitle_reset(&title_state, defer);
    header_drawn     = false;
    title_on_surface = false;
    return before;
}

void ScreenTitle_frameBegin(void) {
    layer_painted_this_frame = false;
}

void ScreenTitle_frameEnd(int* dirty, bool has_header) {
    if (!has_header) {
        // The screen paints the layer through a painter of its own
        header_drawn      = false;
        layer_holds_title = false;
        return;
    }

    uint32_t now = SDL_GetTicks();
    if (header_drawn && ScreenTitle_needsFrame(&title_state, now, title_on_surface)) {
        *dirty = 1;
    }

    if (layer_painted_this_frame) {
        // A row painted the layer this frame, and the title with it
        return;
    }
    if (header_drawn && !title_on_surface) {
        // The title moves, thus each frame draws it
        UiLayer_clear(UI_LAYER_ANIMATION);
        paint_title_on_layer(now);
        layer_painted_this_frame = true;
    } else if (layer_holds_title) {
        // The surface shows the title, thus its last slice leaves the layer
        UiLayer_clear(UI_LAYER_ANIMATION);
        layer_holds_title = false;
    }
}

void render_screen_header(SDL_Surface* screen, const char* text, int show_setting) {
    uint32_t now = SDL_GetTicks();

    // The status group draws first and gives its width, thus the title takes the room
    // that is left and never runs under the wifi and battery pill. The width changes
    // with the clock, with Bluetooth and with the signal, thus only the platform knows
    // it.
    int status_w = 0;
    if (screen_has_status_group(screen)) {
        status_w = GFX_blitHardwareGroup(screen, show_setting);
    }

    // The title takes the font of a row of the list, as the menu of the platform
    // does. The secondary text role keeps it apart from a row.
    TTF_Font* font = Fonts_getLarge();
    if (!ScreenTitle_isCurrent(&title_state, text)) {
        int text_w = 0;
        TTF_SizeUTF8(font, text, &text_w, NULL);
        ScreenTitle_set(&title_state, text, text_w, now);
    }

    header_area  = screen_title_area(screen, status_w, TTF_FontHeight(font));
    header_drawn = true;
    // The surface and the layer reach the display in one present, thus a title
    // at rest on the surface leaves the layer in the same frame.
    title_on_surface = paint_screen_title(screen, &title_state, font,
                                          Theme_getColor(THEME_ROLE_SECONDARY, false),
                                          header_area, now);
}

// Adjust scroll offset to keep selected item visible. Forwards to list_nav.c,
// which owns the window math (see list_page_up/down below for the same shape).
void adjust_list_scroll(int selected, int* scroll, int items_per_page) {
    ListNav_adjustScroll(selected, scroll, items_per_page);
}

// Page math lives in list_nav.c, which has no platform includes and is unit
// tested on the host. These remain so the pre-ListNav call sites keep working;
// they are the same implementation, not a second one.
bool list_page_up(int *selected, int *scroll, int total_count, int items_per_page) {
    return ListNav_pageUp(selected, scroll, total_count, items_per_page);
}

bool list_page_down(int *selected, int *scroll, int total_count, int items_per_page) {
    return ListNav_pageDown(selected, scroll, total_count, items_per_page);
}

void draw_scroll_indicator(SDL_Surface* screen, int asset, int x, int y) {
    // The scroll assets of the platform are 24 by 6 units (`api.c`, `asset_rects`).
    int w = SCALE1(24);
    int h = SCALE1(6);

    // The asset is a dark shape, thus a tint can only make it darker. Its alpha
    // is the shape, and the color of the role fills it. The indicator is a hint
    // beside the list, thus it takes the secondary role, which stays visible on
    // the page of each theme.
    SDL_Surface* shape = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!shape) return;
    SDL_FillRect(shape, NULL, 0);
    GFX_blitAsset(asset, NULL, shape, &(SDL_Rect){0, 0});

    uint32_t rgb = Theme_getPackedColor(THEME_ROLE_SECONDARY, false) & 0x00ffffffu;
    uint32_t* px = shape->pixels;
    for (int i = 0; i < w * h; i++) {
        px[i] = (px[i] & 0xff000000u) | rgb;
    }

    SDL_SetSurfaceBlendMode(shape, SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(shape, NULL, screen, &(SDL_Rect){x, y});
    SDL_FreeSurface(shape);
}

int scroll_indicator_height(void) {
    // The scroll assets of the platform are 6 units high (`api.c`, `asset_rects`).
    return SCALE1(6);
}

int scroll_indicator_x(SDL_Surface* screen) {
    return (screen->w - SCALE1(24)) / 2;
}

// Draws the up indicator where `above` and the down indicator at `down_y` where
// `below`. The up indicator sits above the first row, in the room that the top
// pill row leaves.
static void draw_list_scroll_indicators(SDL_Surface* screen, const ListLayout* layout,
                                        bool above, bool below, int down_y) {
    int ox = scroll_indicator_x(screen);
    if (above) draw_scroll_indicator(screen, ASSET_SCROLL_UP, ox, layout->list_y - scroll_indicator_height());
    if (below) draw_scroll_indicator(screen, ASSET_SCROLL_DOWN, ox, down_y);
}

void render_stream_scroll_indicators(SDL_Surface* screen, const ListLayout* layout, int scroll,
                                     int content_h) {
    // The content ends at the foot of the viewport, thus the indicator is under it
    draw_list_scroll_indicators(screen, layout, scroll > 0, scroll + layout->list_h < content_h,
                                layout->list_y + layout->list_h);
}

void render_scroll_indicators(SDL_Surface* screen, const ListLayout* layout, int scroll,
                              int total_count) {
    if (total_count <= layout->items_per_page) return;
    // Directly under the last row of the page
    draw_list_scroll_indicators(screen, layout, scroll > 0,
                                scroll + layout->items_per_page < total_count,
                                layout->list_y + layout->items_per_page * layout->item_h);
}

// ============================================
// Generic List Rendering Helpers
// ============================================

// Calculate standard list layout based on screen dimensions
ListLayout calc_list_layout(SDL_Surface* screen) {
    int hw = screen->w;
    int hh = screen->h;

    ListLayout layout;
    // The first row starts where the top pill row ends, as a row of the menu of the
    // platform does, and the list stops one scroll indicator above the bottom pill
    // row, which holds the button hints. The indicator takes that room, and not the
    // margin of the footer.
    layout.list_y = SCALE1(PADDING + PILL_SIZE);
    layout.list_h = hh - layout.list_y - SCALE1(PADDING + PILL_SIZE) - scroll_indicator_height();
    layout.item_h = SCALE1(PILL_SIZE);
    layout.items_per_page = layout.list_h / layout.item_h;
    // "Rich" rows (thumbnail + two text lines) are 1.5x a plain row.
    layout.rich_item_h = SCALE1(PILL_SIZE) * 3 / 2;
    layout.rich_items_per_page = layout.list_h / layout.rich_item_h;
    layout.max_width = hw - SCALE1(PADDING * 2);

    return layout;
}

// Render a list item's text with optional scrolling for selected items
void render_list_item_text(SDL_Surface* screen, ScrollTextState* scroll_state,
                           const char* text, TTF_Font* font_param,
                           int text_x, int text_y, int max_text_width,
                           bool selected) {
    SDL_Color text_color = Theme_getColor(THEME_ROLE_PRIMARY, selected);

    // Set clip rect to prevent any text overflow beyond pill boundary
    // Intersect with existing clip to stay within viewport bounds
    SDL_Rect old_clip;
    SDL_GetClipRect(screen, &old_clip);
    SDL_Rect clip = {text_x, text_y, max_text_width, TTF_FontHeight(font_param)};
    if (old_clip.w > 0 && old_clip.h > 0) {
        int left = clip.x > old_clip.x ? clip.x : old_clip.x;
        int top = clip.y > old_clip.y ? clip.y : old_clip.y;
        int right = (clip.x + clip.w) < (old_clip.x + old_clip.w) ? (clip.x + clip.w) : (old_clip.x + old_clip.w);
        int bottom = (clip.y + clip.h) < (old_clip.y + old_clip.h) ? (clip.y + clip.h) : (old_clip.y + old_clip.h);
        if (right > left && bottom > top) {
            clip = (SDL_Rect){left, top, right - left, bottom - top};
        } else {
            return;  // Entirely outside viewport, skip rendering
        }
    }
    SDL_SetClipRect(screen, &clip);

    if (selected && scroll_state) {
        // Selected item: use scrolling text (GPU mode with pill bg)
        ScrollText_update(scroll_state, text, font_param, max_text_width,
                          THEME_ROLE_PRIMARY, true, screen, text_x, text_y, true);
    } else {
        // Non-selected items: static rendering with clipping
        SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font_param, text, text_color);
        if (text_surf) {
            SDL_Rect src = {0, 0, text_surf->w > max_text_width ? max_text_width : text_surf->w, text_surf->h};
            SDL_BlitSurface(text_surf, &src, screen, &(SDL_Rect){text_x, text_y, 0, 0});
            SDL_FreeSurface(text_surf);
        }
    }

    // Restore previous clip rect
    if (old_clip.w > 0 && old_clip.h > 0)
        SDL_SetClipRect(screen, &old_clip);
    else
        SDL_SetClipRect(screen, NULL);
}

// The height of a chip, which a caller needs before it draws one.
int chip_height(void) {
    return TTF_FontHeight(Fonts_getTiny()) + SCALE1(CHIP_PADDING_Y * 2);
}

// A chip: a short label in a rectangular outline with no fill. The player
// screens use one to name the source of what plays. Gives the rectangle that it
// drew, thus a caller can place what follows beside it.
SDL_Rect draw_chip(SDL_Surface* screen, const char* text, int x, int y) {
    SDL_Surface* label = TTF_RenderUTF8_Blended(
        Fonts_getTiny(), text, Theme_getColor(THEME_ROLE_SECONDARY, false));
    SDL_Rect box = {x, y, 0, label ? label->h + SCALE1(CHIP_PADDING_Y * 2) : SCALE1(CHIP_MIN_HEIGHT)};
    if (!label) return box;

    box.w = label->w + SCALE1(CHIP_PADDING_X * 2);
    uint32_t outline = Theme_getPackedColor(THEME_ROLE_SECONDARY, false);
    SDL_FillRect(screen, &(SDL_Rect){box.x, box.y, box.w, 1}, outline);
    SDL_FillRect(screen, &(SDL_Rect){box.x, box.y + box.h - 1, box.w, 1}, outline);
    SDL_FillRect(screen, &(SDL_Rect){box.x, box.y, 1, box.h}, outline);
    SDL_FillRect(screen, &(SDL_Rect){box.x + box.w - 1, box.y, 1, box.h}, outline);
    SDL_BlitSurface(label, NULL, screen,
                    &(SDL_Rect){box.x + SCALE1(CHIP_PADDING_X), box.y + SCALE1(CHIP_PADDING_Y)});
    SDL_FreeSurface(label);
    return box;
}

// The band behind a whole row, for a row that carries a label at its right edge.
void draw_list_item_band(SDL_Surface* screen, SDL_Rect* rect) {
    // The fill argument marks a pill with no separate center color.
    //noinspection HardcodedColor
    GFX_blitPillColor(ASSET_WHITE_PILL, screen, rect,
                      Theme_getPackedColor(THEME_ROLE_BAND_BACKGROUND, false), RGB_WHITE);
}

void draw_list_item_bg(SDL_Surface* screen, SDL_Rect* rect, bool selected) {
    if (selected) {
        // The fill argument marks a pill with no separate center color.
        //noinspection HardcodedColor
        GFX_blitPillColor(ASSET_WHITE_PILL, screen, rect,
                          Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true), RGB_WHITE);
    }
}

// Get the width of a list row, and the text that fits inside it. The text goes
// to "truncated". "prefix_width" is the space that something before the text
// takes, such as an icon or an indicator; give 0 where there is none. A row
// whose text does not fit takes the whole of max_width.
static int calc_list_item_width(TTF_Font* font, const char* text, char* truncated,
                                int max_width, int prefix_width) {
    int available_width = max_width - prefix_width;
    int padding = SCALE1(BUTTON_PADDING * 2);

    // Check if text fits without truncation
    int raw_text_w, raw_text_h;
    TTF_SizeUTF8(font, text, &raw_text_w, &raw_text_h);

    if (raw_text_w + padding > available_width) {
        // Text needs truncation - extend pill to full width (no right padding gap)
        GFX_truncateText(font, text, truncated, available_width, padding);
        return max_width;
    }

    // Text fits - use actual text width with padding
    strncpy(truncated, text, 255);
    truncated[255] = '\0';
    return MIN(max_width, prefix_width + raw_text_w + padding);
}

ListItemPos render_list_item_pill_right(SDL_Surface* screen, ListLayout* layout,
                                        const char* text, char* truncated,
                                        int y, bool selected, int prefix_width,
                                        int right_width) {
    ListItemPos pos;

    // A label at the right edge keeps its own space. The title truncates before
    // it, thus the two never overlap whatever the length of the title.
    // A label wider than the row would leave nothing for the title, thus the
    // reservation stops where the title still has its padding and its prefix.
    int title_max_width = layout->max_width;
    if (right_width > 0) {
        int floor_width = prefix_width + SCALE1(BUTTON_PADDING * 2);
        int reserved = right_width + SCALE1(PADDING * 2);
        if (reserved > title_max_width - floor_width) {
            reserved = title_max_width - floor_width;
        }
        if (reserved > 0) title_max_width -= reserved;
    }

    // Calculate text width for pill sizing (list items use medium font)
    pos.pill_width = calc_list_item_width(Fonts_getMedium(), text, truncated, title_max_width, prefix_width);

    SDL_Rect pill_rect = {SCALE1(PADDING), y, pos.pill_width, layout->item_h};
    draw_list_item_bg(screen, &pill_rect, selected);

    // Calculate text position
    pos.text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    pos.text_y = y + (layout->item_h - TTF_FontHeight(Fonts_getMedium())) / 2;

    return pos;
}

ListItemPos render_list_item_pill(SDL_Surface* screen, ListLayout* layout,
                                   const char* text, char* truncated,
                                   int y, bool selected, int prefix_width) {
    return render_list_item_pill_right(screen, layout, text, truncated, y,
                                       selected, prefix_width, 0);
}

// Render a 2-row list item pill with optional right-side badge area
// Height is 1.5x PILL_SIZE (same as rich). Title (medium) + subtitle (small) inside pill.
// When badge_width > 0 and selected: the band behind the row + the selection pill around the title
// When badge_width == 0: a single selection pill
ListItemBadgedPos render_list_item_pill_badged(SDL_Surface* screen, ListLayout* layout,
                                                const char* text, const char* subtitle,
                                                char* truncated,
                                                int y, bool selected, int badge_width,
                                                int extra_subtitle_width) {
    ListItemBadgedPos pos;

    int item_h = SCALE1(PILL_SIZE) * 3 / 2;

    // Badge area: badge content + BUTTON_PADDING on each side
    int badge_area_w = badge_width > 0 ? badge_width + SCALE1(BUTTON_PADDING * 2) : 0;

    // Calculate title pill width (reduced max to leave room for badge area)
    int title_max_width = layout->max_width - badge_area_w;
    pos.pill_width = calc_list_item_width(Fonts_getMedium(), text, truncated, title_max_width, 0);

    // Expand pill if subtitle is wider than title
    if (subtitle && subtitle[0]) {
        int sub_w;
        TTF_SizeUTF8(Fonts_getSmall(), subtitle, &sub_w, NULL);
        sub_w += extra_subtitle_width;
        int sub_pill_w = MIN(title_max_width, sub_w + SCALE1(BUTTON_PADDING * 2));
        if (sub_pill_w > pos.pill_width)
            pos.pill_width = sub_pill_w;
    }

    if (selected) {
        int px = SCALE1(PADDING);

        if (badge_area_w > 0) {
            // Layer 1: the band, covering the title and the badge area
            int total_w = pos.pill_width + badge_area_w;
            int r = item_h / 3;
            if (r > total_w / 2) r = total_w / 2;
            if (item_h - 2 * r > 0) {
                SDL_FillRect(screen, &(SDL_Rect){px, y + r, total_w, item_h - 2 * r}, Theme_getPackedColor(THEME_ROLE_BAND_BACKGROUND, false));
            }
            for (int dy = 0; dy < r; dy++) {
                int yd = r - dy;
                int inset = r - (int)sqrtf((float)(r * r - yd * yd));
                int row_w = total_w - 2 * inset;
                if (row_w <= 0) continue;
                SDL_FillRect(screen, &(SDL_Rect){px + inset, y + dy, row_w, 1}, Theme_getPackedColor(THEME_ROLE_BAND_BACKGROUND, false));
                SDL_FillRect(screen, &(SDL_Rect){px + inset, y + item_h - 1 - dy, row_w, 1}, Theme_getPackedColor(THEME_ROLE_BAND_BACKGROUND, false));
            }
        }

        // Layer 2 (or only layer): the selection pill around the title
        {
            int pw = pos.pill_width;
            int r = item_h / 3;
            if (r > pw / 2) r = pw / 2;
            if (item_h - 2 * r > 0) {
                SDL_FillRect(screen, &(SDL_Rect){px, y + r, pw, item_h - 2 * r}, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true));
            }
            for (int dy = 0; dy < r; dy++) {
                int yd = r - dy;
                int inset = r - (int)sqrtf((float)(r * r - yd * yd));
                int row_w = pw - 2 * inset;
                if (row_w <= 0) continue;
                SDL_FillRect(screen, &(SDL_Rect){px + inset, y + dy, row_w, 1}, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true));
                SDL_FillRect(screen, &(SDL_Rect){px + inset, y + item_h - 1 - dy, row_w, 1}, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true));
            }
        }
    }

    // Text positions: two rows vertically centered (like rich)
    int text_start_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    int medium_h = TTF_FontHeight(Fonts_getMedium());
    int small_h = TTF_FontHeight(Fonts_getSmall());
    int total_text_h = medium_h + small_h;
    int top_gap = (item_h - total_text_h) / 2;

    pos.text_x = text_start_x;
    pos.text_y = y + top_gap;

    pos.subtitle_x = text_start_x;
    pos.subtitle_y = y + top_gap + medium_h;

    // Badge position (centered vertically in capsule)
    pos.badge_x = SCALE1(PADDING) + pos.pill_width + SCALE1(BUTTON_PADDING);
    pos.badge_y = y + (item_h - TTF_FontHeight(Fonts_getTiny())) / 2;

    // Account for right-side capsule radius reducing usable text width
    int r = item_h / 2;
    pos.text_max_width = pos.pill_width - SCALE1(BUTTON_PADDING) - r / 2;

    pos.total_width = pos.pill_width + badge_area_w;

    return pos;
}

// Render a 2-row list item pill with image area on the left
// Height is 1.5x PILL_SIZE, fits 4 items per page
ListItemRichPos render_list_item_pill_rich(SDL_Surface* screen, ListLayout* layout,
                                            const char* title, const char* subtitle,
                                            char* truncated,
                                            int y, bool selected, bool has_image,
                                            int extra_subtitle_width) {
    ListItemRichPos pos;

    int item_h = SCALE1(PILL_SIZE) * 3 / 2;
    int img_padding = SCALE1(4);

    // Image area: only reserve space when image is available
    int image_area_w;
    if (has_image) {
        pos.image_size = item_h - img_padding * 2;
        image_area_w = img_padding + pos.image_size + SCALE1(BUTTON_PADDING);
        pos.image_x = SCALE1(PADDING) + img_padding;
        pos.image_y = y + img_padding;
    } else {
        pos.image_size = 0;
        image_area_w = SCALE1(BUTTON_PADDING);  // Just left text padding
        pos.image_x = 0;
        pos.image_y = 0;
    }

    // Calculate pill width considering both title and subtitle
    pos.pill_width = calc_list_item_width(Fonts_getMedium(), title, truncated, layout->max_width, image_area_w);
    if (subtitle && subtitle[0]) {
        int sub_w;
        TTF_SizeUTF8(Fonts_getSmall(), subtitle, &sub_w, NULL);
        int sub_pill_w = MIN(layout->max_width, image_area_w + sub_w + extra_subtitle_width + SCALE1(BUTTON_PADDING * 2));
        if (sub_pill_w > pos.pill_width)
            pos.pill_width = sub_pill_w;
    }

    // Draw background (rounded rectangle with reduced radius)
    if (selected) {
        int px = SCALE1(PADDING);
        int pw = pos.pill_width;
        int r = item_h / 3;
        if (r > pw / 2) r = pw / 2;

        // Main body between corner rows
        if (item_h - 2 * r > 0) {
            SDL_FillRect(screen, &(SDL_Rect){px, y + r, pw, item_h - 2 * r}, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true));
        }
        // Top and bottom corner rows with circular inset
        for (int dy = 0; dy < r; dy++) {
            int yd = r - dy;
            int inset = r - (int)sqrtf((float)(r * r - yd * yd));
            int row_w = pw - 2 * inset;
            if (row_w <= 0) continue;
            SDL_FillRect(screen, &(SDL_Rect){px + inset, y + dy, row_w, 1}, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true));
            SDL_FillRect(screen, &(SDL_Rect){px + inset, y + item_h - 1 - dy, row_w, 1}, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true));
        }
    }

    // Text positions: two rows vertically centered
    int text_start_x = SCALE1(PADDING) + image_area_w;
    int medium_h = TTF_FontHeight(Fonts_getMedium());
    int small_h = TTF_FontHeight(Fonts_getSmall());
    int total_text_h = medium_h + small_h;
    int top_gap = (item_h - total_text_h) / 2;

    pos.title_x = text_start_x;
    pos.title_y = y + top_gap;

    pos.subtitle_x = text_start_x;
    pos.subtitle_y = y + top_gap + medium_h;

    pos.text_max_width = pos.pill_width - image_area_w - SCALE1(BUTTON_PADDING);

    return pos;
}

// Render a menu item's pill background and calculate text position
// Menu items have larger spacing (PILL_SIZE + BUTTON_MARGIN) but pill height is just PILL_SIZE
// prefix_width: extra width to account for (e.g., icon)
MenuItemPos render_menu_item_pill(SDL_Surface* screen, ListLayout* layout,
                                   const char* text, char* truncated,
                                   int index, bool selected, int prefix_width) {
    MenuItemPos pos;

    int item_h = SCALE1(PILL_SIZE);
    pos.item_y = layout->list_y + index * item_h;

    // Calculate text width for pill sizing (include prefix_width for icon)
    // One subtraction of the prefix: the helper makes the room for it.
    pos.pill_width = calc_list_item_width(Fonts_getLarge(), text, truncated, layout->max_width, prefix_width);

    // Background pill (pill height is PILL_SIZE, not item_h)
    SDL_Rect pill_rect = {SCALE1(PADDING), pos.item_y, pos.pill_width, SCALE1(PILL_SIZE)};
    draw_list_item_bg(screen, &pill_rect, selected);

    // Calculate text position (centered within PILL_SIZE, not item_h)
    pos.text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    pos.text_y = pos.item_y + (SCALE1(PILL_SIZE) - TTF_FontHeight(Fonts_getLarge())) / 2;

    return pos;
}

// ============================================
// Rounded Rectangle Background
// ============================================

// Render a filled rounded rectangle with smooth circular corners.
// Corner radius is SCALE1(2), clamped to half the width/height.
// Works at any size — unlike pill asset which requires PILL_SIZE height.
void render_rounded_rect_bg(SDL_Surface* screen, int x, int y, int w, int h, uint32_t color) {
    int r = SCALE1(7);
    if (r > h / 4) r = h / 4;
    if (r > w / 4) r = w / 4;

    // Main body between corner rows (full width)
    if (h - 2 * r > 0) {
        SDL_FillRect(screen, &(SDL_Rect){x, y + r, w, h - 2 * r}, color);
    }

    // Top and bottom corner rows with circular inset
    for (int dy = 0; dy < r; dy++) {
        int yd = r - dy;
        int inset = r - (int)sqrtf((float)(r * r - yd * yd));
        int row_w = w - 2 * inset;
        if (row_w <= 0) continue;
        // Top row
        SDL_FillRect(screen, &(SDL_Rect){x + inset, y + dy, row_w, 1}, color);
        // Bottom row (mirrored)
        SDL_FillRect(screen, &(SDL_Rect){x + inset, y + h - 1 - dy, row_w, 1}, color);
    }
}

// ============================================
// Generic Simple Menu Rendering
// ============================================

// Render a simple menu with optional customization callbacks
void render_simple_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                        int menu_scroll, const SimpleMenuConfig* config) {
    GFX_clear(screen);
    char truncated[256];
    char label_buffer[256];

    render_screen_header(screen, config->title, show_setting);
    ListLayout layout = calc_list_layout(screen);

    // Calculate icon size and spacing (scale 24px icons to fit in PILL_SIZE)
    int icon_size = SCALE1(24);
    int icon_spacing = SCALE1(6);

    adjust_list_scroll(menu_selected, &menu_scroll, layout.items_per_page);

    for (int i = menu_scroll;
         i < config->item_count && i - menu_scroll < layout.items_per_page;
         i++) {
        bool selected = (i == menu_selected);

        // Get label (use callback if provided)
        const char* label = config->items[i];
        if (config->get_label) {
            const char* custom = config->get_label(i, label, label_buffer, sizeof(label_buffer));
            if (custom) label = custom;
        }

        // Check if we have an icon for this item
        SDL_Surface* icon = NULL;
        int icon_offset = 0;
        if (config->get_icon) {
            icon = config->get_icon(i, selected);
            if (icon) {
                icon_offset = icon_size + icon_spacing;
            }
        }

        // Render pill and text (account for icon width in pill calculation)
        MenuItemPos pos = render_menu_item_pill(screen, &layout, label, truncated,
                                                i - menu_scroll, selected, icon_offset);

        // Render icon if present (scale to display size)
        int text_x = pos.text_x;
        if (icon) {
            int icon_y = pos.item_y + (SCALE1(PILL_SIZE) - icon_size) / 2;
            SDL_Rect src_rect = {0, 0, icon->w, icon->h};
            SDL_Rect dst_rect = {pos.text_x, icon_y, icon_size, icon_size};
            SDL_BlitScaled(icon, &src_rect, screen, &dst_rect);
            text_x += icon_offset;
        }

        // Render text after icon (use custom callback if provided)
        bool custom_rendered = false;
        if (config->render_text) {
            custom_rendered = config->render_text(screen, i, selected,
                                                   text_x, pos.text_y, layout.max_width - icon_offset);
        }
        if (!custom_rendered) {
            render_list_item_text(screen, NULL, truncated, Fonts_getLarge(),
                                  text_x, pos.text_y, layout.max_width - icon_offset, selected);
        }

        // Render badge if callback provided
        if (config->render_badge) {
            config->render_badge(screen, i, selected, pos.item_y, SCALE1(PILL_SIZE));
        }
    }

    render_scroll_indicators(screen, &layout, menu_scroll, config->item_count);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", (char*)config->btn_b_label, "A", "OPEN", NULL}, 1, screen, 1);
}


// ============================================
// Dialog Box
// ============================================

DialogBox render_dialog_box(SDL_Surface* screen, int box_w, int box_h) {
    // The marquee under the dialog leaves the layer. The title of the header
    // under the dialog leaves it too, until the header draws again.
    UiLayer_clear(UI_LAYER_ANIMATION);
    header_drawn = false;
    layer_holds_title = false;

    int hw = screen->w;
    int hh = screen->h;

    DialogBox db;
    db.box_w = box_w;
    db.box_h = box_h;
    db.box_x = (hw - box_w) / 2;
    db.box_y = (hh - box_h) / 2;
    db.content_x = db.box_x + SCALE1(15);
    db.content_w = box_w - SCALE1(30);

    // Paint the page background around the dialog.
    SDL_Rect top_area = {0, 0, hw, db.box_y};
    SDL_Rect bot_area = {0, db.box_y + box_h, hw, hh - db.box_y - box_h};
    SDL_Rect left_area = {0, db.box_y, db.box_x, box_h};
    SDL_Rect right_area = {db.box_x + box_w, db.box_y, hw - db.box_x - box_w, box_h};
    SDL_FillRect(screen, &top_area, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, false));
    SDL_FillRect(screen, &bot_area, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, false));
    SDL_FillRect(screen, &left_area, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, false));
    SDL_FillRect(screen, &right_area, Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, false));

    // Box background
    SDL_FillRect(screen, &(SDL_Rect){db.box_x, db.box_y, box_w, box_h},
                 Theme_getPackedColor(THEME_ROLE_SURFACE_BACKGROUND, false));

    // Box border
    SDL_FillRect(screen, &(SDL_Rect){db.box_x, db.box_y, box_w, SCALE1(2)},
                 Theme_getPackedColor(THEME_ROLE_PRIMARY, false));
    SDL_FillRect(screen, &(SDL_Rect){db.box_x, db.box_y + box_h - SCALE1(2), box_w, SCALE1(2)},
                 Theme_getPackedColor(THEME_ROLE_PRIMARY, false));
    SDL_FillRect(screen, &(SDL_Rect){db.box_x, db.box_y, SCALE1(2), box_h},
                 Theme_getPackedColor(THEME_ROLE_PRIMARY, false));
    SDL_FillRect(screen, &(SDL_Rect){db.box_x + box_w - SCALE1(2), db.box_y, SCALE1(2), box_h},
                 Theme_getPackedColor(THEME_ROLE_PRIMARY, false));

    return db;
}

void render_empty_state(SDL_Surface* screen, const char* message,
                        const char* subtitle, const char* y_button_label) {
    int hw = screen->w;
    int hh = screen->h;
    int center_y = hh / 2 - SCALE1(15);

    SDL_Surface* icon = Icons_getEmpty(THEME_ROLE_PRIMARY, false);
    if (icon) {
        int icon_size = SCALE1(48);
        SDL_Rect src_rect = {0, 0, icon->w, icon->h};
        SDL_Rect dst_rect = {(hw - icon_size) / 2, center_y - SCALE1(40), icon_size, icon_size};
        SDL_BlitScaled(icon, &src_rect, screen, &dst_rect);
        center_y += icon_size / 2;
    }

    SDL_Surface* text1 = TTF_RenderUTF8_Blended(
        Fonts_getMedium(), message, Theme_getColor(THEME_ROLE_PRIMARY, false));
    if (text1) {
        SDL_BlitSurface(text1, NULL, screen, &(SDL_Rect){(hw - text1->w) / 2, center_y - SCALE1(10)});
        SDL_FreeSurface(text1);
    }

    if (subtitle) {
        SDL_Surface* text2 = TTF_RenderUTF8_Blended(
            Fonts_getSmall(), subtitle, Theme_getColor(THEME_ROLE_SECONDARY, false));
        if (text2) {
            SDL_BlitSurface(text2, NULL, screen, &(SDL_Rect){(hw - text2->w) / 2, center_y + SCALE1(10)});
            SDL_FreeSurface(text2);
        }
    }

    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (y_button_label) {
        GFX_blitButtonGroup((char*[]){"Y", (char*)y_button_label, "B", "BACK", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }
}
