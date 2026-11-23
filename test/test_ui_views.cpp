// Tests for src/ui/view/text_view.cpp, src/ui/view/title_text_view.cpp, src/ui/view/list_view.cpp and
// src/ui/view/list_screen_view.cpp - the text views, the row of a list, the list and the layout of a
// list screen, composed by the compositor of src/ui/view/ui_compositor.cpp.
//
// The layer is a test double of ui_layers.c, as in test_ui_compositor.cpp.

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "test.h"
#include "ui/view/bitmap_view.hpp"
#include "ui/view/box_view.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/empty_state_view.hpp"
#include "ui/view/list_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/view/multiline_text_view.hpp"
#include "ui/view/progress_bar_view.hpp"
#include "ui/view/scroll_view.hpp"
#include "ui/view/status_group_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui/view/title_text_view.hpp"
#include "input/common_button_handler.hpp"
#include "input/input_driver.hpp"
#include "ui/confirm_dialog_page.hpp"
#include "ui/controls_help_page.hpp"
#include "ui/database_error_dialog_page.hpp"
#include "ui/quit_dialog_page.hpp"
#include "ui/page/page_stack.hpp"
#include "ui/view/dialog_view.hpp"
#include "ui/page/page_context.hpp"
#include "ui/view/ui_compositor.hpp"
#include "ui_layers.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

namespace {

constexpr int kScreenW = 400;
constexpr int kScreenH = 120;

std::vector<uint32_t> layerPixels;
int layerWidth = 0;

}  // namespace

extern "C" void UiLayer_clear(UiLayer layer) {
    if (layer == UI_LAYER_ANIMATION) layerPixels.assign(layerPixels.size(), 0);
}

extern "C" void UiLayer_blit(SDL_Surface* surface, int x, int y, UiLayer layer) {
    if (layer != UI_LAYER_ANIMATION) return;
    for (int row = 0; row < surface->h; row++) {
        const uint32_t* from = reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(surface->pixels) +
                                                                 row * surface->pitch);
        for (int column = 0; column < surface->w; column++) {
            layerPixels[(y + row) * layerWidth + x + column] = from[column];
        }
    }
}

namespace {

TTF_Font* testFont = nullptr;

constexpr const char* kLongText = "a text that is far wider than the space that the view has for it";
constexpr SDL_Color kRed        = {255, 0, 0, 255};

struct Fixture {
    SDL_Surface* screen;
    UiCompositor compositor;
    std::unique_ptr<PageContext> contextHolder = PageContext::create(compositor);
    PageContext& context                       = *contextHolder;

    explicit Fixture(int width = kScreenW, int height = kScreenH) {
        screen = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
        layerWidth = width;
        layerPixels.assign(static_cast<size_t>(width * height), 0);
        compositor.setScreen(screen);
    }
    ~Fixture() { SDL_FreeSurface(screen); }

    uint32_t surfaceAt(int x, int y) const {
        return reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(screen->pixels) + y * screen->pitch)[x];
    }

    // Returns true where a pixel of the rectangle is not transparent
    bool surfaceHasPixels(const SDL_Rect& rect) const {
        for (int y = rect.y; y < rect.y + rect.h; y++) {
            for (int x = rect.x; x < rect.x + rect.w; x++) {
                if (surfaceAt(x, y) >> 24) return true;
            }
        }
        return false;
    }
    bool layerHasPixels(const SDL_Rect& rect) const {
        for (int y = rect.y; y < rect.y + rect.h; y++) {
            for (int x = rect.x; x < rect.x + rect.w; x++) {
                if (layerPixels[y * layerWidth + x] >> 24) return true;
            }
        }
        return false;
    }

    /** Starts and runs one frame of the page, stepMs after the last one. */
    void tick(uint32_t stepMs) {
        elapsed += stepMs;
        context.startFrame(elapsed, context.getActiveTime() + stepMs);
        context.runFrameTasks();
    }

    uint32_t elapsed = 0;

    void frame(UiView& view, const SDL_Rect& rect) {
        UiLayoutContext& root = compositor.beginLayout(context);
        root.layoutChild(view, rect);
        std::vector<TaskOwner> owners;
        root.collectViewIds(owners);
        context.updateActiveOwners(owners);
        compositor.drawFrame();
    }
};

const SDL_Rect kRow = {0, 0, 200, 40};

TEST(test_text_that_fits_is_not_clipped_and_does_not_scroll) {
    Fixture f;
    TextView view(testFont, false);
    view.setText("short");
    f.frame(view, kRow);
    CHECK(!view.isClipped());
    view.startScrolling();
    f.frame(view, kRow);
    f.tick(5000);
    CHECK(!view.isDirty());
    f.frame(view, kRow);
    CHECK(!f.layerHasPixels(kRow));
    CHECK(f.surfaceHasPixels(kRow));
}

// The scroll of a text on the active time of its page

/** Returns the offset of a scroll of ms at speedSp */
int scrolledBy(int speedSp, uint32_t ms) { return SCALE1(speedSp) * static_cast<int>(ms) / 1000; }

TEST(test_scroll_moves_at_its_speed_on_the_active_time) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(16);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), scrolledBy(40, 516));
    f.frame(view, kRow);
    CHECK(f.layerHasPixels(kRow));
}

TEST(test_stop_keeps_the_offset_and_a_new_speed_continues_from_it) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(1000);
    const int stopped = view.getScrollOffset();
    CHECK(stopped > 0);

    for (int speed : {0, -5}) {
        view.startScrolling(40);
        if (speed == 0) view.stopScrolling(); else view.startScrolling(speed);
        f.tick(1000);
        CHECK_EQ_INT(view.getScrollOffset(), stopped);
    }
    view.startScrolling(0);
    f.frame(view, kRow);
    f.tick(1000);
    CHECK_EQ_INT(view.getScrollOffset(), stopped);

    // The time of the stop does not count
    view.startScrolling(80);
    f.frame(view, kRow);
    f.tick(250);
    CHECK_EQ_INT(view.getScrollOffset(), stopped + scrolledBy(80, 250));
}

TEST(test_a_new_speed_in_a_frame_does_not_count_the_time_before_it) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling(40);
    f.tick(500);
    const int before = view.getScrollOffset();

    // As from an input handler: after the start of the frame, before its callbacks
    f.elapsed += 500;
    f.context.startFrame(f.elapsed, f.context.getActiveTime() + 500);
    view.startScrolling(80);
    f.context.runFrameTasks();
    CHECK_EQ_INT(view.getScrollOffset(), before);
    f.tick(250);
    CHECK_EQ_INT(view.getScrollOffset(), before + scrolledBy(80, 250));
}

TEST(test_a_start_before_the_first_layout_scrolls_after_the_attach) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), scrolledBy(40, 500));
}

TEST(test_a_text_that_leaves_the_page_layout_stops_until_the_next_start) {
    Fixture f;
    TextView view(testFont, false);
    TextView other(testFont, false);
    view.setText(kLongText);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(500);
    const int before = view.getScrollOffset();

    // The page layout detaches the view, and the next one attaches it again
    f.context.startLayout();
    f.frame(other, kRow);
    f.context.finishLayout();
    f.tick(5000);
    f.frame(view, kRow);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), before);
}

TEST(test_a_text_that_leaves_the_layout_stops_until_the_next_start) {
    Fixture f;
    TextView view(testFont, false);
    TextView other(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(500);
    const int before = view.getScrollOffset();

    f.frame(other, kRow);
    f.tick(5000);
    CHECK_EQ_INT(view.getScrollOffset(), before);

    f.frame(view, kRow);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), before);

    // The time away does not count
    view.startScrolling(40);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), before + scrolledBy(40, 500));
}

TEST(test_reset_puts_the_text_at_its_start_and_keeps_the_speed) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(1000);
    view.resetScrolling();
    CHECK_EQ_INT(view.getScrollOffset(), 0);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), scrolledBy(40, 500));
}

TEST(test_new_text_puts_the_offset_at_zero_and_keeps_the_speed) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(1000);
    view.setText(std::string(kLongText) + " and more");
    CHECK_EQ_INT(view.getScrollOffset(), 0);
    f.frame(view, kRow);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), scrolledBy(40, 500));
}

TEST(test_text_that_fits_does_not_move) {
    Fixture f;
    TextView view(testFont, false);
    view.setText("short");
    f.frame(view, kRow);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(5000);
    CHECK_EQ_INT(view.getScrollOffset(), 0);
    f.frame(view, kRow);
    CHECK(!f.layerHasPixels(kRow));
}

TEST(test_start_moves_the_text_from_the_next_frame) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    f.tick(1000);
    view.startScrolling(40);
    CHECK_EQ_INT(view.getScrollOffset(), 0);
    f.tick(500);
    CHECK_EQ_INT(view.getScrollOffset(), scrolledBy(40, 500));
}

TEST(test_row_forwards_the_scroll_to_its_whole_text) {
    Fixture f;
    ListRowView row(testFont);
    row.setText(kLongText);
    row.setSelected(true);
    f.frame(row, kRow);
    row.startScrolling();
    f.frame(row, kRow);
    f.tick(1000);
    f.frame(row, kRow);
    CHECK(f.layerHasPixels(kRow));
    row.stopScrolling();
    row.resetScrolling();
    f.frame(row, kRow);
    CHECK(!f.layerHasPixels(kRow));
}

TEST(test_row_scrolls_again_when_its_whole_text_comes_back) {
    Fixture f;
    ListRowView row(testFont);
    row.setText(kLongText);
    row.setSelected(true);
    f.frame(row, kRow);
    row.startScrolling();
    f.frame(row, kRow);
    f.tick(500);
    f.frame(row, kRow);
    CHECK(f.layerHasPixels(kRow));

    // A wide screen shows the short text, and the whole text leaves the layout
    const SDL_Rect wide = {0, 0, 4000, 40};
    f.frame(row, wide);
    f.tick(500);
    f.frame(row, kRow);
    const std::vector<uint32_t> before = layerPixels;
    f.tick(500);
    f.frame(row, kRow);
    CHECK(layerPixels != before);

    // A stopped text does not start again
    row.stopScrolling();
    row.resetScrolling();
    f.frame(row, wide);
    f.frame(row, kRow);
    f.tick(500);
    f.frame(row, kRow);
    CHECK(!f.layerHasPixels(kRow));
}

TEST(test_text_is_as_high_as_a_line_with_the_text_in_the_middle) {
    Fixture f;
    TextView view(testFont, false);
    view.setText("short");
    f.frame(view, kRow);
    const int fontHeight = TTF_FontHeight(testFont);
    const int lineHeight = std::max(TTF_FontLineSkip(testFont), fontHeight);
    CHECK_EQ_INT(view.getSize().h, lineHeight);
    const int top = (lineHeight - fontHeight) / 2;
    CHECK(f.surfaceHasPixels(SDL_Rect{0, top, kRow.w, fontHeight}));
    if (top > 0) CHECK(!f.surfaceHasPixels(SDL_Rect{0, 0, kRow.w, top}));
}

TEST(test_text_takes_no_less_than_the_font_height) {
    Fixture f;
    TextView view(testFont, false);
    view.setText("short");
    f.frame(view, SDL_Rect{0, 0, 200, 1});
    CHECK_EQ_INT(view.getSize().h, TTF_FontHeight(testFont));
}

TEST(test_long_text_with_ellipsis_keeps_one_width) {
    Fixture f;
    TextView view(testFont, true);
    view.setText(kLongText);
    f.frame(view, kRow);
    CHECK(view.isClipped());
    CHECK(view.getSize().w <= kRow.w);
    const int width = view.getSize().w;
    view.startScrolling();
    f.frame(view, kRow);
    f.tick(5000);
    f.frame(view, kRow);
    CHECK_EQ_INT(view.getSize().w, width);
    CHECK(!f.layerHasPixels(kRow));
}

TEST(test_color_change_draws_the_view_again) {
    Fixture f;
    TextView view(testFont, false);
    view.setText("short");
    f.frame(view, kRow);
    view.setColor(kRed);
    CHECK(view.isDirty());
    f.frame(view, kRow);
    CHECK(!view.isDirty());
    bool red = false;
    for (int x = 0; x < view.getSize().w; x++) {
        for (int y = 0; y < view.getSize().h; y++) {
            const uint32_t pixel = f.surfaceAt(x, y);
            if ((pixel >> 24) == 255 && (pixel & 0x00FFFFFF) == 0x00FF0000) red = true;
        }
    }
    CHECK(red);
}

TEST(test_scrolling_text_moves_to_the_layer_and_back) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    CHECK(f.surfaceHasPixels(kRow));
    view.startScrolling();
    f.frame(view, kRow);
    f.tick(1000);
    f.frame(view, kRow);
    CHECK(f.layerHasPixels(kRow));
    CHECK(!f.surfaceHasPixels(kRow));
    view.stopScrolling();
    view.resetScrolling();
    f.frame(view, kRow);
    CHECK(!f.layerHasPixels(kRow));
    CHECK(f.surfaceHasPixels(kRow));
}

TEST(test_same_offset_in_whole_pixels_is_not_dirty) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling(40);
    f.frame(view, kRow);
    f.tick(1000);
    f.frame(view, kRow);
    f.tick(1);
    CHECK(!view.isDirty());
    f.tick(500);
    CHECK(view.isDirty());
}

TEST(test_surface_only_mode_shows_the_text_at_its_offset) {
    Fixture f;
    TextView view(testFont, false);
    view.setText(kLongText);
    f.frame(view, kRow);
    view.startScrolling();
    f.frame(view, kRow);
    f.tick(1000);
    f.frame(view, kRow);
    f.compositor.setSurfaceOnly(true);
    f.frame(view, kRow);
    CHECK(f.surfaceHasPixels(kRow));
    CHECK(!f.layerHasPixels(kRow));
}

TEST(test_setter_with_the_same_value_is_not_dirty) {
    Fixture f;
    TextView view(testFont, false);
    view.setText("short");
    view.setColor(kRed);
    f.frame(view, kRow);
    view.setText("short");
    view.setColor(kRed);
    CHECK(!view.isDirty());
}

TEST(test_transparent_box_has_no_content_until_it_gets_a_color) {
    Fixture f;
    BoxView box;
    const SDL_Rect rect = {0, 0, 8, 8};
    f.frame(box, rect);
    CHECK(!f.surfaceHasPixels(rect));

    box.setColor(SDL_Color{255, 0, 0, 128});
    CHECK(box.isDirty());
    f.frame(box, rect);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0x80800000);

    box.setColor(SDL_Color{0, 0, 0, 0});
    f.frame(box, rect);
    CHECK(!f.surfaceHasPixels(rect));
}

TEST(test_edge_pixel_on_the_surface_is_premultiplied) {
    Fixture f;
    TextView view(testFont, false);
    view.setText("short");
    view.setColor(kRed);
    f.frame(view, kRow);
    bool found = false;
    for (int y = 0; y < view.getSize().h && !found; y++) {
        for (int x = 0; x < view.getSize().w && !found; x++) {
            const uint32_t pixel = f.surfaceAt(x, y);
            const uint32_t alpha = pixel >> 24;
            if (alpha > 40 && alpha < 200) {
                found = true;
                // A premultiplied full red has its red channel at its alpha
                const uint32_t red = (pixel >> 16) & 0xFF;
                CHECK(red + 2 >= alpha && red <= alpha + 2);
            }
        }
    }
    CHECK(found);
}

TEST(test_bitmap_is_scaled_into_the_view) {
    Fixture f;
    BitmapView view;
    SDL_Surface* bitmap = SDL_CreateRGBSurfaceWithFormat(0, 2, 2, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(bitmap, nullptr, SDL_MapRGBA(bitmap->format, 255, 0, 0, 255));
    view.setBitmap(bitmap);
    const SDL_Rect rect = {10, 10, 20, 20};
    f.frame(view, rect);
    CHECK_EQ_SZ(f.surfaceAt(10, 10), 0xFFFF0000);
    CHECK_EQ_SZ(f.surfaceAt(29, 29), 0xFFFF0000);
    CHECK_EQ_SZ(f.surfaceAt(30, 30), 0u);
}

TEST(test_bitmap_view_loads_an_image_from_memory) {
    SDL_Surface* image = SDL_CreateRGBSurfaceWithFormat(0, 3, 2, 32, SDL_PIXELFORMAT_ARGB8888);
    std::vector<uint8_t> buffer(1024);
    SDL_RWops* out = SDL_RWFromMem(buffer.data(), static_cast<int>(buffer.size()));
    CHECK(SDL_SaveBMP_RW(image, out, 0) == 0);
    const size_t size = static_cast<size_t>(SDL_RWtell(out));
    SDL_RWclose(out);
    SDL_FreeSurface(image);

    BitmapView view;
    CHECK(view.loadMemory(buffer.data(), size));
    CHECK(view.hasBitmap());
    const uint8_t garbage[] = {1, 2, 3, 4};
    CHECK(!view.loadMemory(garbage, sizeof(garbage)));
    CHECK(!view.hasBitmap());
}

TEST(test_bitmap_view_does_not_load_a_missing_file) {
    BitmapView view;
    CHECK(!view.loadFile("no-such-image.png"));
    CHECK(!view.hasBitmap());
}

TEST(test_bitmap_view_frees_its_bitmap) {
    SDL_Surface* bitmap = SDL_CreateRGBSurfaceWithFormat(0, 2, 2, 32, SDL_PIXELFORMAT_ARGB8888);
    bitmap->refcount++;
    {
        BitmapView view;
        view.setBitmap(bitmap);
    }
    CHECK_EQ_SZ(bitmap->refcount, 1);
    bitmap->refcount++;
    {
        BitmapView view;
        view.setBitmap(bitmap);
        view.setBitmap(nullptr);
        CHECK_EQ_SZ(bitmap->refcount, 1);
        CHECK(!view.hasBitmap());
    }
    SDL_FreeSurface(bitmap);
}

uint32_t opaque(SDL_Color color) {
    return 0xFF000000u | (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

TEST(test_progress_bar_fills_its_percent) {
    Fixture f;
    ProgressBarView view(testFont, 10);
    const SDL_Rect rect = {0, 0, 200, 10};
    const uint32_t fillColor  = opaque(Theme_getColor(THEME_ROLE_PROGRESS_FILL, false));
    const uint32_t trackColor = opaque(Theme_getColor(THEME_ROLE_PROGRESS_TRACK, false));

    f.frame(view, rect);
    const int middle = view.getSize().h / 2;
    CHECK(view.getSize().h >= 10);
    CHECK_EQ_SZ(f.surfaceAt(1, middle), trackColor);

    view.setPercent(50);
    f.frame(view, rect);
    CHECK_EQ_SZ(f.surfaceAt(1, middle), fillColor);
    CHECK_EQ_SZ(f.surfaceAt(198, middle), trackColor);

    view.setPercent(100);
    f.frame(view, rect);
    CHECK_EQ_SZ(f.surfaceAt(198, middle), fillColor);
}

TEST(test_progress_bar_clamps_a_value_out_of_the_range) {
    ProgressTrackView track;
    track.setPercent(150);
    CHECK_EQ_SZ(track.getPercent(), 100);
    track.setPercent(-5);
    CHECK_EQ_SZ(track.getPercent(), 0);
}

// Returns the highest alpha in the column x of the rectangle
uint32_t maxAlphaInColumn(const Fixture& f, int x, int height) {
    uint32_t alpha = 0;
    for (int y = 0; y < height; y++) alpha = std::max(alpha, f.surfaceAt(x, y) >> 24);
    return alpha;
}

TEST(test_title_fades_at_the_cut_and_not_when_it_fits) {
    Fixture f;
    TitleTextView title(testFont);
    title.setText(kLongText);
    f.frame(title, SDL_Rect{0, 0, 200, 40});
    CHECK(maxAlphaInColumn(f, 199, 40) < 40);
    bool opaqueBeforeFade = false;
    for (int x = 0; x < 150; x++) opaqueBeforeFade = opaqueBeforeFade || maxAlphaInColumn(f, x, 40) > 200;
    CHECK(opaqueBeforeFade);

    // A new width moves the fade
    f.frame(title, SDL_Rect{0, 0, 300, 40});
    CHECK(maxAlphaInColumn(f, 299, 40) < 40);

    TitleTextView shortTitle(testFont);
    shortTitle.setText("I");
    Fixture g;
    g.frame(shortTitle, SDL_Rect{0, 0, 200, 40});
    const int last = shortTitle.getSize().w - 1;
    bool opaqueAtEnd = false;
    for (int x = 0; x <= last; x++) opaqueAtEnd = opaqueAtEnd || maxAlphaInColumn(g, x, 40) > 200;
    CHECK(opaqueAtEnd);
}

TEST(test_row_shows_long_text_only_when_selected) {
    Fixture f;
    ListRowView row(testFont);
    row.setText(kLongText);
    row.startScrolling();
    f.frame(row, kRow);
    f.tick(1000);
    f.frame(row, kRow);
    CHECK(!f.layerHasPixels(kRow));

    row.setSelected(true);
    row.startScrolling();
    f.frame(row, kRow);
    f.tick(1000);
    f.frame(row, kRow);
    CHECK(f.layerHasPixels(kRow));

    Fixture g;
    ListRowView shortRow(testFont);
    shortRow.setText("short");
    shortRow.setSelected(true);
    shortRow.startScrolling();
    g.frame(shortRow, kRow);
    g.tick(1000);
    g.frame(shortRow, kRow);
    CHECK(!g.layerHasPixels(kRow));
}

TEST(test_selection_change_makes_the_row_dirty) {
    Fixture f;
    ListRowView row(testFont);
    row.setText("short");
    f.frame(row, kRow);
    CHECK(!row.isDirty());
    row.setSelected(true);
    CHECK(row.isDirty());
    f.frame(row, kRow);
    CHECK(!row.isDirty());
}

// A view of a fixed height that counts its draws and shows its selection.
class TestItem : public ListItemView {
public:
    void layout(UiLayoutContext& context) override {
        context.setSize(UiSize{context.getMaxSize().w, 10});
    }
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override {
        SDL_FillRect(context.getSurface(), nullptr, selected ? 0xFFFF0000 : 0xFF00FF00);
        dirty = false;
        draws++;
    }
    void setSelected(bool value) override {
        if (selected == value) return;
        selected = value;
        dirty    = true;
    }

    bool selected = false;
    bool dirty    = true;
    int draws     = 0;
};

class TestAdapter : public ListAdapter {
public:
    std::vector<long> ids;
    int created = 0;
    /** Each view that the adapter made, in order */
    std::vector<std::shared_ptr<ListItemView>> made;

    int getCount() const override { return static_cast<int>(ids.size()); }
    long getItemId(int index) const override { return ids[static_cast<size_t>(index)]; }
    std::shared_ptr<ListItemView> getView(int) override {
        created++;
        made.push_back(std::make_shared<TestItem>());
        return made.back();
    }
};

TestItem& itemAt(ListView& list, int index) { return static_cast<TestItem&>(list.getItemView(index)); }

// Six items of 10 pixels in a list of 35: three whole items show
const SDL_Rect kList = {0, 0, 100, 35};

TEST(test_cursor_move_draws_only_the_two_changed_rows) {
    Fixture f;
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4, 5, 6};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(0);
    f.frame(list, kList);
    CHECK(list.moveForward());
    f.frame(list, kList);
    CHECK_EQ_INT(itemAt(list, 0).draws, 2);
    CHECK_EQ_INT(itemAt(list, 1).draws, 2);
    CHECK_EQ_INT(itemAt(list, 2).draws, 1);
}

TEST(test_move_selects_the_new_row_and_clears_the_old_row) {
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4, 5, 6};
    ListView list;
    list.setAdapter(adapter);
    CHECK(itemAt(list, 0).selected);
    CHECK(list.moveForward());
    CHECK(!itemAt(list, 0).selected);
    CHECK(itemAt(list, 1).selected);
    CHECK(list.moveBackward());
    CHECK(itemAt(list, 0).selected);
    CHECK(!itemAt(list, 1).selected);
    list.setSelectedIndex(4);
    CHECK(!itemAt(list, 0).selected);
    CHECK(itemAt(list, 4).selected);
}

TEST(test_page_move_selects_the_new_row) {
    Fixture f;
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4, 5, 6};
    ListView list;
    list.setAdapter(adapter);
    f.frame(list, kList);
    CHECK(list.pageForward());
    CHECK(!itemAt(list, 0).selected);
    CHECK(itemAt(list, list.getSelectedIndex()).selected);
}

TEST(test_data_change_selects_only_the_row_of_the_cursor) {
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(2);
    // A new row before the cursor, and the row of the cursor stays
    adapter.ids = {9, 1, 2, 3, 4};
    list.notifyDataChanged();
    CHECK_EQ_INT(list.getSelectedIndex(), 3);
    for (int index = 0; index < list.getItemCount(); index++) {
        CHECK(itemAt(list, index).selected == (index == 3));
    }
    // The row of the cursor goes away, thus the cursor stays at its index
    adapter.ids = {9, 1, 2, 4};
    list.notifyDataChanged();
    CHECK_EQ_INT(list.getSelectedIndex(), 3);
    for (int index = 0; index < list.getItemCount(); index++) {
        CHECK(itemAt(list, index).selected == (index == 3));
    }
}

TEST(test_removed_item_moves_the_rows_with_no_draw) {
    Fixture f;
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(0);
    f.frame(list, kList);
    TestItem* third = &itemAt(list, 2);
    adapter.ids     = {1, 3, 4};
    list.notifyDataChanged();
    f.frame(list, kList);
    CHECK(&itemAt(list, 1) == third);
    CHECK_EQ_INT(third->draws, 1);
    CHECK_EQ_INT(adapter.created, 4);
    CHECK_EQ_SZ(f.surfaceAt(1, 15), 0xFF00FF00);
}

TEST(test_move_past_the_last_visible_row_scrolls) {
    Fixture f;
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4, 5, 6};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(0);
    f.frame(list, kList);
    CHECK_EQ_INT(list.getFirstVisibleIndex(), 0);
    CHECK_EQ_INT(list.getLastVisibleIndex(), 2);
    list.setSelectedIndex(3);
    f.frame(list, kList);
    CHECK_EQ_INT(list.getFirstVisibleIndex(), 1);
    CHECK_EQ_INT(list.getLastVisibleIndex(), 3);
}

TEST(test_row_that_the_clip_cuts_is_hidden) {
    Fixture f;
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(0);
    f.frame(list, kList);
    // The fourth item would cover the rows 30 to 39, and the list ends at 35
    CHECK(!f.surfaceHasPixels(SDL_Rect{0, 30, 100, 5}));
    CHECK(f.surfaceHasPixels(SDL_Rect{0, 20, 100, 10}));
}

TEST(test_page_needs_the_layout_after_a_data_change) {
    Fixture f;
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4, 5, 6};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(0);
    CHECK(!list.pageForward());
    f.frame(list, kList);
    CHECK(list.pageForward());
    CHECK_EQ_INT(list.getSelectedIndex(), 3);
    CHECK(list.pageBackward());
    CHECK_EQ_INT(list.getSelectedIndex(), 0);
}

TEST(test_move_past_an_end_wraps_to_the_other_end) {
    TestAdapter adapter;
    adapter.ids = {1, 2, 3};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(2);
    CHECK(list.moveForward());
    CHECK_EQ_INT(list.getSelectedIndex(), 0);
    CHECK(list.moveBackward());
    CHECK_EQ_INT(list.getSelectedIndex(), 2);
}

TEST(test_single_item_does_not_move) {
    TestAdapter adapter;
    adapter.ids = {1};
    ListView list;
    list.setAdapter(adapter);
    CHECK(!list.moveForward());
    CHECK(!list.moveBackward());
    CHECK_EQ_INT(list.getSelectedIndex(), 0);
}

TEST(test_empty_list_gives_minus_one) {
    Fixture f;
    TestAdapter adapter;
    ListView list;
    list.setAdapter(adapter);
    f.frame(list, kList);
    CHECK_EQ_INT(list.getSelectedIndex(), -1);
    CHECK_EQ_INT(list.getFirstVisibleIndex(), -1);
    CHECK_EQ_INT(list.getLastVisibleIndex(), -1);
    CHECK(!list.moveForward());
}

constexpr int kBigW = 1024;
constexpr int kBigH = 768;

int fakeGroupWidth = 200;
int fakeGroupDraws = 0;
StatusValues fakeValues;

StatusValues readFakeValues() {
    return fakeValues;
}

// Draws a half transparent group where the platform draws the status group
int drawFakeGroup(SDL_Surface* dst, int) {
    fakeGroupDraws++;
    SDL_Surface* piece = SDL_CreateRGBSurfaceWithFormat(0, fakeGroupWidth, SCALE1(PILL_SIZE), 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(piece, nullptr, 0x80FFFFFF);
    SDL_SetSurfaceBlendMode(piece, SDL_BLENDMODE_BLEND);
    SDL_Rect at = {dst->w - SCALE1(PADDING) - fakeGroupWidth, SCALE1(PADDING), 0, 0};
    SDL_BlitSurface(piece, nullptr, dst, &at);
    SDL_FreeSurface(piece);
    return fakeGroupWidth;
}

TEST(test_status_group_refresh_with_the_same_pixels_changes_nothing) {
    Fixture f(kBigW, kBigH);
    fakeGroupWidth = 200;
    StatusGroupView status(drawFakeGroup, readFakeValues);
    f.frame(status, SDL_Rect{0, 0, kBigW, SCALE1(PILL_SIZE)});
    CHECK(f.compositor.hasSurfaceChanged());
    status.refresh();
    f.frame(status, SDL_Rect{0, 0, kBigW, SCALE1(PILL_SIZE)});
    CHECK(!status.isDirty());
    CHECK(!f.compositor.hasSurfaceChanged());
}

TEST(test_status_group_refresh_gives_the_same_pixels) {
    Fixture f(kBigW, kBigH);
    fakeGroupWidth = 200;
    StatusGroupView status(drawFakeGroup, readFakeValues);
    f.frame(status, SDL_Rect{0, 0, kBigW, SCALE1(PILL_SIZE)});
    const uint32_t once = f.surfaceAt(10, 10);
    CHECK((once >> 24) > 0);
    for (int i = 0; i < 10; i++) {
        status.refresh();
        f.frame(status, SDL_Rect{0, 0, kBigW, SCALE1(PILL_SIZE)});
    }
    CHECK_EQ_SZ(f.surfaceAt(10, 10), once);
}

// Returns the base values, but a wifi signal of -1 on the second read of a render
StatusValues readValuesThatChangeInTheDraw() {
    static int reads = 0;
    StatusValues values = fakeValues;
    if (++reads == 2) values.connection = -1;
    return values;
}

// Lays out the status group in a frame at the time now
void statusFrame(Fixture& f, StatusGroupView& status, uint32_t now) {
    input::InputState::getInstance().setState(input::PadState{0, 0, 0}, now);
    f.frame(status, SDL_Rect{0, 0, kBigW, SCALE1(PILL_SIZE)});
}

TEST(test_status_group_renders_when_a_value_settles) {
    Fixture f(kBigW, kBigH);
    fakeGroupWidth = 200;
    fakeValues     = StatusValues{};
    StatusGroupView status(drawFakeGroup, readFakeValues);
    statusFrame(f, status, 1000);
    fakeGroupDraws = 0;
    for (uint32_t now = 1100; now < 1600; now += 100) {
        statusFrame(f, status, now);
    }
    CHECK_EQ_INT(fakeGroupDraws, 0);
    fakeValues.minuteOfDay = 1;
    statusFrame(f, status, 2000);
    statusFrame(f, status, 2000 + StatusGroupView::kSettleMs - 1);
    CHECK_EQ_INT(fakeGroupDraws, 0);
    statusFrame(f, status, 2000 + StatusGroupView::kSettleMs);
    CHECK_EQ_INT(fakeGroupDraws, 1);
    statusFrame(f, status, 4000);
    CHECK_EQ_INT(fakeGroupDraws, 1);
}

TEST(test_status_group_renders_again_after_a_change_during_the_render) {
    Fixture f(kBigW, kBigH);
    fakeGroupWidth = 200;
    fakeValues     = StatusValues{};
    StatusGroupView status(drawFakeGroup, readValuesThatChangeInTheDraw);
    statusFrame(f, status, 1000);
    fakeGroupDraws = 0;
    statusFrame(f, status, 1100);
    statusFrame(f, status, 1000 + StatusGroupView::kSettleMs);
    CHECK_EQ_INT(fakeGroupDraws, 0);
    statusFrame(f, status, 1100 + StatusGroupView::kSettleMs);
    CHECK_EQ_INT(fakeGroupDraws, 1);
    statusFrame(f, status, 1100 + 3 * StatusGroupView::kSettleMs);
    CHECK_EQ_INT(fakeGroupDraws, 1);
}

TEST(test_status_group_ignores_a_value_that_comes_back) {
    Fixture f(kBigW, kBigH);
    fakeGroupWidth = 200;
    fakeValues     = StatusValues{};
    StatusGroupView status(drawFakeGroup, readFakeValues);
    statusFrame(f, status, 1000);
    fakeGroupDraws        = 0;
    fakeValues.connection = -1;
    statusFrame(f, status, 2000);
    fakeValues.connection = 0;
    statusFrame(f, status, 2100);
    statusFrame(f, status, 2000 + 2 * StatusGroupView::kSettleMs);
    CHECK_EQ_INT(fakeGroupDraws, 0);
}

TEST(test_shorter_status_group_leaves_no_old_pixels) {
    Fixture f(kBigW, kBigH);
    fakeGroupWidth = 200;
    StatusGroupView status(drawFakeGroup, readFakeValues);
    f.frame(status, SDL_Rect{0, 0, kBigW, SCALE1(PILL_SIZE)});
    CHECK_EQ_INT(status.getSize().w, 200);
    fakeGroupWidth = 100;
    status.refresh();
    f.frame(status, SDL_Rect{0, 0, kBigW, SCALE1(PILL_SIZE)});
    CHECK_EQ_INT(status.getSize().w, 100);
    CHECK((f.surfaceAt(50, 10) >> 24) > 0);
    CHECK_EQ_SZ(f.surfaceAt(150, 10), 0);
}

// A view of a given size, or of the size that its parent offers, in one color
class SizedView : public UiView {
public:
    SizedView(uint32_t color, UiSize size = {0, 0}) : color(color), wanted(size) {}

    void layout(UiLayoutContext& context) override {
        const UiSize size = wanted.w > 0 ? wanted : context.getMaxSize();
        context.setSize(size);
        context.setTargetLayer(target);
    }
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override {
        SDL_FillRect(context.getSurface(), nullptr, color);
        dirty = false;
        draws++;
    }

    uint32_t color;
    UiSize wanted;
    UiTargetLayer target = UiTargetLayer::MainSurface;
    bool dirty           = true;
    int draws            = 0;
};

constexpr uint32_t kRightColor = 0xFF0000FF;
constexpr UiSize kRightSize   = {20, 10};

// The rectangle of the right view in kRow
SDL_Rect rightViewRect() {
    const int padding = SCALE1(BUTTON_PADDING);
    const int x       = kRow.w - padding - kRightSize.w;
    const int y       = (std::min(kRow.h, SCALE1(PILL_SIZE)) - kRightSize.h) / 2;
    return SDL_Rect{x, y, kRightSize.w, kRightSize.h};
}

TEST(test_right_view_shows_at_the_right_edge_of_the_row) {
    Fixture f;
    ListRowView row(testFont);
    row.setText("short");
    row.setRightView(std::make_shared<SizedView>(kRightColor, kRightSize));
    f.frame(row, kRow);
    const SDL_Rect right = rightViewRect();
    CHECK_EQ_SZ(f.surfaceAt(right.x, right.y), kRightColor);
    CHECK_EQ_SZ(f.surfaceAt(right.x + right.w - 1, right.y + right.h - 1), kRightColor);
}

TEST(test_long_text_ends_before_the_right_view) {
    Fixture f;
    ListRowView row(testFont);
    row.setText(kLongText);
    row.setRightView(std::make_shared<SizedView>(kRightColor, kRightSize));
    f.frame(row, kRow);
    const SDL_Rect right = rightViewRect();
    const int padding    = SCALE1(BUTTON_PADDING);
    CHECK(f.surfaceHasPixels(SDL_Rect{padding, 0, right.x - padding * 3, kRow.h}));
    CHECK(!f.surfaceHasPixels(SDL_Rect{right.x - padding * 2, 0, padding * 2, kRow.h}));
}

TEST(test_right_view_set_after_the_first_layout_attaches_at_the_next_layout) {
    Fixture f;
    ListRowView row(testFont);
    f.frame(row, kRow);
    // Records that it got a context
    class ContextView : public SizedView {
    public:
        ContextView() : SizedView(kRightColor, kRightSize) {}
        bool gotContext = false;

    protected:
        void onAttach(UiContext&) override { gotContext = true; }
    };
    auto right = std::make_shared<ContextView>();
    row.setRightView(right);
    CHECK(!right->gotContext);
    f.frame(row, kRow);
    CHECK(right->gotContext);
}

TEST(test_removed_right_view_makes_the_row_dirty) {
    Fixture f;
    ListRowView row(testFont);
    row.setText("short");
    row.setRightView(std::make_shared<SizedView>(kRightColor, kRightSize));
    f.frame(row, kRow);
    CHECK(!row.isDirty());
    row.setRightView(nullptr);
    CHECK(row.isDirty());
    f.frame(row, kRow);
    CHECK(!f.surfaceHasPixels(rightViewRect()));
}

TEST(test_new_right_view_of_a_selected_row_draws_the_row_again) {
    Fixture f;
    ListRowView row(testFont);
    row.setText("short");
    row.setSelected(true);
    f.frame(row, kRow);
    CHECK(!row.isDirty());
    row.setRightView(std::make_shared<SizedView>(kRightColor, kRightSize));
    CHECK(row.isDirty());
    f.frame(row, kRow);
    CHECK(!row.isDirty());
    CHECK_EQ_SZ(f.surfaceAt(rightViewRect().x, rightViewRect().y), kRightColor);
}

constexpr uint32_t kTitleColor = 0xFF101010;
constexpr uint32_t kUpColor    = 0xFF202020;
constexpr uint32_t kDownColor  = 0xFF303030;
constexpr uint32_t kHintsColor = 0xFF404040;

/** The views of a list screen, as stand-ins of one color each */
struct StandInParts {
    UiView& titleView;
    UiView& statusView;
    ListView& listView;
    UiView& scrollUpView;
    UiView& scrollDownView;
    UiView& buttonHintsView;
};

// The places of a list screen, as ListScreenView gives them, over stand-in views
class TestListScreen : public UiLayout {
public:
    explicit TestListScreen(const StandInParts& parts) : parts(parts) {}

    void layout(UiLayoutContext& context) override {
        const UiSize screen = context.getMaxSize();
        const int padding   = SCALE1(PADDING);
        const int top       = layoutScreenTop(context, parts.titleView, parts.statusView);
        const int bottom    = layoutScreenBottom(context, parts.buttonHintsView);
        const SDL_Rect body = {padding, top, screen.w - padding * 2, bottom - top};

        const SDL_Rect wholeScreen    = {0, 0, screen.w, screen.h};
        UiLayoutContext upMeasuring   = context.createMeasuringContext();
        UiLayoutContext downMeasuring = context.createMeasuringContext();
        const UiSize upSize           = upMeasuring.layoutChild(parts.scrollUpView, wholeScreen).getSize();
        const UiSize downSize         = downMeasuring.layoutChild(parts.scrollDownView, wholeScreen).getSize();
        context.layoutChild(parts.listView, body);
        context.layoutChild(parts.scrollUpView,
                            SDL_Rect{(screen.w - upSize.w) / 2, body.y - upSize.h, upSize.w, upSize.h});
        const SDL_Rect downRect = {(screen.w - downSize.w) / 2, body.y + parts.listView.getShownBottom(), downSize.w,
                                   downSize.h};
        context.layoutChild(parts.scrollDownView, downRect);
        parts.scrollUpView.setVisible(parts.listView.canScrollUp());
        parts.scrollDownView.setVisible(parts.listView.canScrollDown());
    }

private:
    StandInParts parts;
};

struct ScreenFixture {
    Fixture f{kBigW, kBigH};
    SizedView title{kTitleColor};
    SizedView status{0xFF505050, UiSize{200, SCALE1(PILL_SIZE)}};
    SizedView up{kUpColor, UiSize{SCALE1(24), SCALE1(6)}};
    SizedView down{kDownColor, UiSize{SCALE1(24), SCALE1(6)}};
    SizedView hints{kHintsColor};
    TestAdapter adapter;
    ListView list;
    TestListScreen listScreen{StandInParts{
        .titleView       = title,
        .statusView      = status,
        .listView        = list,
        .scrollUpView    = up,
        .scrollDownView  = down,
        .buttonHintsView = hints,
    }};

    ScreenFixture() {
        for (long id = 1; id <= 60; id++) adapter.ids.push_back(id);
        list.setAdapter(adapter);
        list.setSelectedIndex(0);
        f.frame(listScreen, SDL_Rect{0, 0, kBigW, kBigH});
    }
    void frame() { f.frame(listScreen, SDL_Rect{0, 0, kBigW, kBigH}); }

    // The middle of the up indicator, where it overlaps the title
    SDL_Point upPoint() const { return SDL_Point{kBigW / 2, SCALE1(PADDING + PILL_SIZE) - 2}; }
};

TEST(test_up_indicator_shows_over_the_title_with_no_title_draw) {
    ScreenFixture s;
    CHECK_EQ_SZ(s.f.surfaceAt(s.upPoint().x, s.upPoint().y), kTitleColor);
    s.list.setSelectedIndex(55);
    s.frame();
    CHECK(s.list.getFirstVisibleIndex() > 0);
    CHECK_EQ_SZ(s.f.surfaceAt(s.upPoint().x, s.upPoint().y), kUpColor);
    CHECK_EQ_INT(s.title.draws, 1);
}

TEST(test_new_title_draws_the_title_under_the_indicator) {
    ScreenFixture s;
    s.list.setSelectedIndex(55);
    s.frame();
    s.title.dirty = true;
    s.frame();
    CHECK_EQ_INT(s.title.draws, 2);
    CHECK_EQ_SZ(s.f.surfaceAt(s.upPoint().x, s.upPoint().y), kUpColor);
    CHECK_EQ_SZ(s.f.surfaceAt(100, 50), kTitleColor);
}

TEST(test_scrolling_title_frame_changes_the_layer_only) {
    ScreenFixture s;
    s.title.target = UiTargetLayer::AnimationLayer;
    s.frame();
    s.title.dirty = true;
    s.frame();
    CHECK(s.f.compositor.hasLayersChanged());
    CHECK(!s.f.compositor.hasSurfaceChanged());
}

TEST(test_hints_change_keeps_the_down_indicator_over_them) {
    ScreenFixture s;
    // The rows of 10 pixels reach the bottom of the list, thus the down indicator overlaps the hints
    const int hintsTop   = kBigH - SCALE1(PADDING + PILL_SIZE);
    const SDL_Point over = {kBigW / 2, hintsTop + 2};
    CHECK_EQ_SZ(s.f.surfaceAt(over.x, over.y), kDownColor);
    s.hints.dirty = true;
    s.frame();
    CHECK_EQ_INT(s.hints.draws, 2);
    CHECK_EQ_SZ(s.f.surfaceAt(over.x, over.y), kDownColor);
    CHECK_EQ_SZ(s.f.surfaceAt(10, hintsTop + 2), kHintsColor);
}

TEST(test_new_status_width_moves_the_title) {
    ScreenFixture s;
    const int before = s.title.getSize().w;
    s.status.wanted  = UiSize{100, SCALE1(PILL_SIZE)};
    s.frame();
    CHECK_EQ_INT(s.title.getSize().w, before + 100);
    CHECK_EQ_INT(s.title.draws, 2);
}

/** Returns the layout of `view` under `context`, or null. */
const UiLayoutContext* findLayout(const UiLayoutContext& context, const UiView& view) {
    if (context.getView() == &view) return &context;
    for (const UiLayoutContext& child : context.getChildren()) {
        if (const UiLayoutContext* found = findLayout(child, view)) return found;
    }
    return nullptr;
}

/** Lays out `view` on the whole surface of `f` with no draw, and returns the layout of `view`. */
const UiLayoutContext& layoutOnly(Fixture& f, UiView& view, const SDL_Rect& rect) {
    UiLayoutContext& root = f.compositor.beginLayout(f.context);
    return root.layoutChild(view, rect);
}

class TestScrolling : public ScrollingView {
public:
    bool up         = false;
    bool down       = false;
    int shownBottom = 0;

    bool canScrollUp() const override { return up; }
    bool canScrollDown() const override { return down; }
    int getShownBottom() const override { return shownBottom; }
    void layout(UiLayoutContext& context) override { context.setSize(context.getMaxSize()); }
};

const SDL_Rect kWholeBig = {0, 0, kBigW, kBigH};

int screenBodyTop() { return SCALE1(PADDING + PILL_SIZE); }
int screenBodyBottom() { return kBigH - SCALE1(PADDING + PILL_SIZE); }

bool rectEquals(const SDL_Rect& a, const SDL_Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/** Returns the rectangle of the layout of `view` in the screen, or an empty one. */
SDL_Rect screenRectOf(const UiLayoutContext& screen, const UiView& view) {
    const UiLayoutContext* found = findLayout(screen, view);
    return found ? found->getScreenRect() : SDL_Rect{0, 0, 0, 0};
}

TEST(test_screen_content_gets_the_body_inside_its_margins) {
    Fixture f(kBigW, kBigH);
    ScreenView screen(testFont);
    SizedView content(kTitleColor);
    const int padding = SCALE1(PADDING);
    const int top     = screenBodyTop();
    const int height  = screenBodyBottom() - top;

    screen.setContent(&content);
    CHECK(rectEquals(screenRectOf(layoutOnly(f, screen, kWholeBig), content), SDL_Rect{0, top, kBigW, height}));
    screen.setContent(&content, ContentMargins::Sides);
    CHECK(rectEquals(screenRectOf(layoutOnly(f, screen, kWholeBig), content),
                     SDL_Rect{padding, top, kBigW - padding * 2, height}));
    screen.setContent(&content, ContentMargins::SidesAndFoot);
    CHECK(rectEquals(screenRectOf(layoutOnly(f, screen, kWholeBig), content),
                     SDL_Rect{padding, top, kBigW - padding * 2, height - padding}));
}

TEST(test_content_with_no_scroll_has_no_scroll_indicators) {
    Fixture f(kBigW, kBigH);
    ScreenView screen(testFont);
    SizedView content(kTitleColor);
    screen.setContent(&content);
    // The status group, the title, the hints and the content
    CHECK_EQ_SZ(layoutOnly(f, screen, kWholeBig).getChildren().size(), 4u);
}

TEST(test_scrolling_content_gets_the_scroll_indicators) {
    Fixture f(kBigW, kBigH);
    ScreenView screen(testFont);
    TestScrolling content;
    content.up          = true;
    content.shownBottom = 30;
    screen.setScrollingContent(&content, ContentMargins::Sides);
    const UiLayoutContext& layout = layoutOnly(f, screen, kWholeBig);

    // The indicators are the last two children, after the content
    CHECK_EQ_SZ(layout.getChildren().size(), 6u);
    const UiLayoutContext& up   = *std::next(layout.getChildren().begin(), 4);
    const UiLayoutContext& down = *std::next(layout.getChildren().begin(), 5);
    const UiSize size           = {SCALE1(24), SCALE1(6)};
    const int top               = screenBodyTop();
    CHECK(rectEquals(up.getScreenRect(), SDL_Rect{(kBigW - size.w) / 2, top - size.h, size.w, size.h}));
    CHECK(rectEquals(down.getScreenRect(), SDL_Rect{(kBigW - size.w) / 2, top + 30, size.w, size.h}));
    CHECK(up.getView()->isVisible());
    CHECK(!down.getView()->isVisible());

    content.up   = false;
    content.down = true;
    const UiLayoutContext& again = layoutOnly(f, screen, kWholeBig);
    CHECK(!std::next(again.getChildren().begin(), 4)->getView()->isVisible());
    CHECK(std::next(again.getChildren().begin(), 5)->getView()->isVisible());
}

TEST(test_list_tells_where_it_can_scroll) {
    Fixture f;
    TestAdapter adapter;
    adapter.ids = {1, 2, 3, 4, 5, 6};
    ListView list;
    list.setAdapter(adapter);
    list.setSelectedIndex(0);
    f.frame(list, kList);
    CHECK(!list.canScrollUp());
    CHECK(list.canScrollDown());
    CHECK_EQ_INT(list.getShownBottom(), 30);
    list.setSelectedIndex(5);
    f.frame(list, kList);
    CHECK(list.canScrollUp());
    CHECK(!list.canScrollDown());
    CHECK_EQ_INT(list.getShownBottom(), 30);
}

TEST(test_empty_list_cannot_scroll) {
    Fixture f;
    TestAdapter adapter;
    ListView list;
    list.setAdapter(adapter);
    f.frame(list, kList);
    CHECK(!list.canScrollUp());
    CHECK(!list.canScrollDown());
    CHECK_EQ_INT(list.getShownBottom(), 0);
}

TEST(test_scroll_view_tells_where_it_can_scroll) {
    Fixture f;
    SizedView text(kTitleColor, UiSize{100, 100});
    ScrollView scroll;
    scroll.setContent(&text);
    const SDL_Rect rect = {0, 0, 100, 40};
    f.frame(scroll, rect);
    CHECK(!scroll.canScrollUp());
    CHECK(scroll.canScrollDown());
    CHECK_EQ_INT(scroll.getShownBottom(), 40);
    scroll.scrollBy(100);
    f.frame(scroll, rect);
    CHECK(scroll.canScrollUp());
    CHECK(!scroll.canScrollDown());
    CHECK_EQ_INT(scroll.getShownBottom(), 40);
}

TEST(test_list_screen_lays_out_the_list_inside_the_side_margins) {
    Fixture f(kBigW, kBigH);
    TestAdapter adapter;
    for (long id = 1; id <= 60; id++) adapter.ids.push_back(id);
    ListScreenView screen(testFont);
    screen.setAdapter(adapter);
    const UiLayoutContext& layout = layoutOnly(f, screen, kWholeBig);
    const int padding             = SCALE1(PADDING);
    const int top                 = screenBodyTop();
    // The status group, the title, the hints, the list and the two indicators
    CHECK_EQ_SZ(layout.getChildren().size(), 6u);
    const UiLayoutContext& content = *std::next(layout.getChildren().begin(), 3);
    const ListView& list           = static_cast<const ListView&>(*content.getView());
    CHECK(rectEquals(content.getScreenRect(), SDL_Rect{padding, top, kBigW - padding * 2, screenBodyBottom() - top}));
    CHECK_EQ_INT(list.getItemCount(), 60);
    const UiLayoutContext& down = *std::next(layout.getChildren().begin(), 5);
    CHECK_EQ_INT(down.getScreenRect().y, top + list.getShownBottom());
    CHECK(down.getView()->isVisible());
}

TEST(test_empty_state_replaces_the_list_and_the_indicators) {
    Fixture f(kBigW, kBigH);
    TestAdapter adapter;
    ListScreenView screen(testFont);
    // The test has no fonts of the app, thus the empty state lays out no text
    class TestEmptyState : public EmptyStateView {
    public:
        void layout(UiLayoutContext& context) override { context.setSize(context.getMaxSize()); }
    } empty;
    screen.setAdapter(adapter);
    screen.setEmptyState(&empty);
    const UiLayoutContext& layout = layoutOnly(f, screen, kWholeBig);
    // The status group, the title, the hints and the empty state
    CHECK_EQ_SZ(layout.getChildren().size(), 4u);
    CHECK(std::next(layout.getChildren().begin(), 3)->getView() == &empty);
    adapter.ids = {1};
    screen.setAdapter(adapter);
    const UiLayoutContext& filled = layoutOnly(f, screen, kWholeBig);
    CHECK_EQ_SZ(filled.getChildren().size(), 6u);
    CHECK(findLayout(filled, empty) == nullptr);
}

}  // namespace

// A new title draws the dialog again. A dialog with the same content draws nothing in the next
// frame.
TEST(test_dialog_draws_again_only_for_a_change) {
    Fixture f(kScreenW, kScreenH);
    DialogView dialog(200, DialogView::Alignment::Center, testFont, testFont);
    dialog.setTitle("Quit?");
    dialog.setHint("A: Yes");
    const SDL_Rect screen = {0, 0, kScreenW, kScreenH};
    f.frame(dialog, screen);
    CHECK(f.compositor.hasSurfaceChanged());
    f.frame(dialog, screen);
    CHECK(!f.compositor.hasSurfaceChanged());
    dialog.setTitle("Quit now?");
    f.frame(dialog, screen);
    CHECK(f.compositor.hasSurfaceChanged());
    // The surface of the dialog is in the middle of the space, and the space around it is clear
    CHECK(f.surfaceHasPixels(SDL_Rect{kScreenW / 2 - 4, kScreenH / 2 - 4, 8, 8}));
    CHECK(!f.surfaceHasPixels(SDL_Rect{0, 0, 40, 4}));
}

namespace {

class RecordingPlayback : public input::PlaybackControl {
public:
    std::vector<std::string> commands;

    void togglePlayback() override { commands.push_back("toggle"); }
    void skipTrack(bool next) override { commands.push_back(next ? "next" : "previous"); }
    void changeVolume(int step) override { commands.push_back(step > 0 ? "volume+" : "volume-"); }
};

/** Runs page on a stack, and returns the playback commands of an earphone button */
std::vector<std::string> pressEarphoneButtonOn(const UiPage& page) {
    Fixture f;
    RecordingPlayback playback;
    PageStack stack(playback);
    stack.start(page.createPageImpl(), f.compositor);
    stack.getRawInput().onRawEvent({0, input::event::Media{input::MediaKey::PlayPause}});
    stack.closeAll();
    return playback.commands;
}

/**
 * Runs dialog on a stack, presses the buttons in order and releases them. Returns true when the
 * dialog closed.
 */
bool pressClosesDialog(const UiPage& dialog, std::initializer_list<input::Button> buttons) {
    Fixture f;
    RecordingPlayback playback;
    PageStack stack(playback);
    stack.start(dialog.createPageImpl(), f.compositor);
    uint32_t now = 0;
    auto endFrame = [&] {
        stack.tickInput();
        stack.tickPages();
        stack.endFrame();
        stack.beginPoll();
        now += 16;
        stack.startFrame(now);
    };
    for (input::Button button : buttons) {
        stack.getRawInput().onRawEvent({now, input::event::ButtonPressed{button}});
        endFrame();
    }
    for (input::Button button : buttons) {
        stack.getRawInput().onRawEvent({now, input::event::ButtonReleased{button}});
        endFrame();
    }
    const bool closed = stack.isDone();
    stack.closeAll();
    return closed;
}

}  // namespace

// START closes Controls Help, and START with a volume button is a volume gesture
TEST(test_controls_help_ignores_start_with_a_volume_button) {
    using input::Button;
    const ControlsHelpPage help(HELP_MAIN_MENU);
    CHECK(pressClosesDialog(help, {Button::Start}));
    CHECK(pressClosesDialog(help, {Button::A}));
    CHECK(!pressClosesDialog(help, {Button::Plus, Button::Start}));
    CHECK(!pressClosesDialog(help, {Button::Start, Button::Minus}));
}

TEST(test_quit_dialog_ignores_start_with_a_volume_button) {
    using input::Button;
    CHECK(pressClosesDialog(QuitDialogPage{}, {Button::Start}));
    CHECK(pressClosesDialog(QuitDialogPage{}, {Button::B}));
    CHECK(!pressClosesDialog(QuitDialogPage{}, {Button::Plus, Button::Start}));
    CHECK(!pressClosesDialog(QuitDialogPage{}, {Button::Start, Button::Minus}));
}

namespace {

/** A page that opens a confirmation for a result, and keeps the result. */
class ConfirmParentPage final : public UiPage {
public:
    using Result = std::monostate;
    explicit ConfirmParentPage(std::optional<bool>& result) : result(result) {}
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;

private:
    std::optional<bool>& result;
};

class ConfirmParentPageImpl final : public UiPageImpl<ConfirmParentPage> {
public:
    explicit ConfirmParentPageImpl(std::optional<bool>& result) : UiPageImpl(PageKind::FullScreen), result(result) {}
    void onEnter() override {
        getNavigator().openForResult(ConfirmDialogPage("Clear?"), [this](const bool& yes) { result = yes; });
    }
    void layout(UiLayoutContext&) override {}

private:
    std::optional<bool>& result;
};

std::unique_ptr<UiPageImplBase> ConfirmParentPage::createPageImpl() const {
    return std::make_unique<ConfirmParentPageImpl>(result);
}

/** Opens a confirmation over a page, presses and releases button, and returns the result. */
std::optional<bool> confirmWith(input::Button button) {
    Fixture f;
    RecordingPlayback playback;
    PageStack stack(playback);
    std::optional<bool> result;
    stack.start(ConfirmParentPage(result).createPageImpl(), f.compositor);
    uint32_t now = 0;
    auto frame   = [&] {
        stack.tickInput();
        stack.tickPages();
        stack.endFrame();
        stack.beginPoll();
        now += 16;
        stack.startFrame(now);
    };
    frame();
    stack.getRawInput().onRawEvent({now, input::event::ButtonPressed{button}});
    frame();
    stack.getRawInput().onRawEvent({now, input::event::ButtonReleased{button}});
    frame();
    stack.closeAll();
    return result;
}

}  // namespace

TEST(test_confirm_dialog_gives_yes_on_a_and_no_on_b) {
    CHECK(confirmWith(input::Button::A) == std::optional<bool>(true));
    CHECK(confirmWith(input::Button::B) == std::optional<bool>(false));
    // Another button does not close the dialog
    CHECK(!confirmWith(input::Button::X).has_value());
}

// A dialog page gives the buttons of the earphones to the global handler when no pad button is
// held
TEST(test_dialog_pages_give_the_earphone_buttons_to_the_playback) {
    const std::vector<std::string> want = {"toggle"};
    CHECK(pressEarphoneButtonOn(QuitDialogPage{}) == want);
    CHECK(pressEarphoneButtonOn(ControlsHelpPage(HELP_MAIN_MENU)) == want);
}

namespace input {

// Gives the test the step of the driver that takes a pad state, as test_input_events.cpp does
struct InputDriverTest {
    static void update(InputDriver& driver, const PadState& padState, bool padReset, uint32_t now,
                       RawEventListener& listener) {
        driver.update(padState, padReset, now, listener);
    }
};

}  // namespace input

// The database error dialog closes on each button of the pad
TEST(test_database_error_dialog_closes_on_a_pad_button) {
    using input::Button;
    const DatabaseErrorDialogPage dialog;
    for (Button button : {Button::A, Button::B, Button::X, Button::Y, Button::Start, Button::Select,
                          Button::DpadUp, Button::L1, Button::R2, Button::Menu, Button::Minus}) {
        CHECK(pressClosesDialog(dialog, {button}));
    }
}

// A media key goes to the playback, and the database error dialog stays open
TEST(test_database_error_dialog_stays_open_at_a_media_key) {
    Fixture f;
    RecordingPlayback playback;
    PageStack stack(playback);
    stack.start(DatabaseErrorDialogPage{}.createPageImpl(), f.compositor);
    stack.startFrame(0);
    stack.getRawInput().onRawEvent({0, input::event::Media{input::MediaKey::PlayPause}});
    stack.tickInput();
    stack.tickPages();
    stack.endFrame();
    CHECK(playback.commands == std::vector<std::string>{"toggle"});
    CHECK(!stack.isDone());
    stack.closeAll();
}

// After the reset of the pad, the release of a press from before the reset does not close the
// dialog, and a new press closes it
TEST(test_database_error_dialog_ignores_a_press_from_before_the_reset) {
    using input::Button;
    Fixture f;
    RecordingPlayback playback;
    PageStack stack(playback);
    stack.start(DatabaseErrorDialogPage{}.createPageImpl(), f.compositor);
    input::InputDriver driver;
    const uint32_t a = input::getButtonBit(Button::A);
    uint32_t now     = 0;
    auto frame       = [&](const input::PadState& state, bool reset) {
        stack.startFrame(now);
        stack.beginPoll();
        input::InputDriverTest::update(driver, state, reset, now, stack.getRawInput());
        stack.tickInput();
        stack.tickPages();
        stack.endFrame();
        now += 16;
    };
    frame(input::PadState{.pressed = 0, .justPressed = 0, .justReleased = 0}, true);
    frame(input::PadState{.pressed = 0, .justPressed = 0, .justReleased = a}, false);
    CHECK(!stack.isDone());
    frame(input::PadState{.pressed = a, .justPressed = a, .justReleased = 0}, false);
    frame(input::PadState{.pressed = 0, .justPressed = 0, .justReleased = a}, false);
    CHECK(stack.isDone());
    stack.closeAll();
}

int getMultilineHeight(int lines) {
    return lines * std::max(TTF_FontLineSkip(testFont), TTF_FontHeight(testFont));
}

// A text that is wider than the view goes on more lines, and no line goes past the width
TEST(test_multiline_text_wraps_at_a_space) {
    Fixture f;
    MultilineTextView view(testFont);
    view.setText(kLongText);
    f.frame(view, SDL_Rect{0, 0, 100, kScreenH});
    CHECK(view.getSize().w == 100);
    CHECK(view.getSize().h > getMultilineHeight(1));
    CHECK(view.getSize().h % getMultilineHeight(1) == 0);
    CHECK(f.surfaceHasPixels(SDL_Rect{0, 0, 100, getMultilineHeight(1)}));
    CHECK(!f.surfaceHasPixels(SDL_Rect{100, 0, kScreenW - 100, kScreenH}));
}

// A line break starts a new line, and two line breaks leave an empty line
TEST(test_multiline_text_starts_a_line_at_a_line_break) {
    Fixture f;
    MultilineTextView view(testFont);
    view.setText("a\n\nb");
    f.frame(view, SDL_Rect{0, 0, 200, kScreenH});
    const int line = getMultilineHeight(1);
    CHECK(view.getSize().h == getMultilineHeight(3));
    CHECK(f.surfaceHasPixels(SDL_Rect{0, 0, 200, line}));
    CHECK(!f.surfaceHasPixels(SDL_Rect{0, line, 200, line}));
    CHECK(f.surfaceHasPixels(SDL_Rect{0, 2 * line, 200, line}));
}

// With the wrap by words, a long word after a short one starts a line of its own
TEST(test_multiline_word_wrap_moves_a_long_word_to_the_next_line) {
    Fixture f;
    MultilineTextView view(testFont);
    view.setText("- abcdefghijklmnopqrstuvwxyz");
    f.frame(view, SDL_Rect{0, 0, 100, kScreenH});
    CHECK(!f.surfaceHasPixels(SDL_Rect{50, 0, 50, getMultilineHeight(1)}));
}

// With the wrap by characters, the first line takes each character that fits
TEST(test_multiline_character_wrap_fills_the_line) {
    Fixture f;
    MultilineTextView view(testFont);
    view.setWrap(MultilineTextView::Wrap::Characters);
    view.setText("- abcdefghijklmnopqrstuvwxyz");
    f.frame(view, SDL_Rect{0, 0, 100, kScreenH});
    CHECK(view.getSize().h > getMultilineHeight(1));
    CHECK(f.surfaceHasPixels(SDL_Rect{50, 0, 50, getMultilineHeight(1)}));
    CHECK(!f.surfaceHasPixels(SDL_Rect{100, 0, kScreenW - 100, kScreenH}));
}

// A word that is wider than the view breaks at a character
TEST(test_multiline_long_word_breaks_at_a_character) {
    Fixture f;
    MultilineTextView view(testFont);
    view.setText("abcdefghijklmnopqrstuvwxyz");
    f.frame(view, SDL_Rect{0, 0, 40, kScreenH});
    CHECK(view.getSize().h > getMultilineHeight(1));
    CHECK(!f.surfaceHasPixels(SDL_Rect{40, 0, kScreenW - 40, kScreenH}));
}

// Spans in different roles that fit share a line, one after the other
TEST(test_multiline_spans_share_a_line) {
    Fixture f;
    MultilineTextView view(testFont);
    view.setText(SpannedText().add("ab ").add("cd", THEME_ROLE_SECONDARY));
    f.frame(view, SDL_Rect{0, 0, 200, kScreenH});
    int firstWidth = 0;
    TTF_SizeUTF8(testFont, "ab ", &firstWidth, nullptr);
    CHECK(view.getSize().h == getMultilineHeight(1));
    CHECK(f.surfaceHasPixels(SDL_Rect{firstWidth, 0, 200 - firstWidth, getMultilineHeight(1)}));
}

// A plain text is a text of one span in the primary role
TEST(test_multiline_same_text_is_not_dirty) {
    Fixture f;
    MultilineTextView view(testFont);
    CHECK(view.setText("text"));
    f.frame(view, kRow);
    CHECK(!view.isDirty());
    CHECK(!view.setText("text"));
    CHECK(!view.setText(SpannedText().add("text")));
    CHECK(!view.isDirty());
}

TEST(test_multiline_fixed_line_height_sets_the_height) {
    Fixture f(kScreenW, 200);
    MultilineTextView view(testFont);
    view.setText("one\ntwo\nthree");
    view.setLineHeight(30);
    f.frame(view, SDL_Rect{0, 0, 200, 200});
    CHECK_EQ_SZ(view.getSize().h, 90);
}

TEST(test_multiline_max_lines_shows_the_first_lines) {
    Fixture f(kScreenW, 200);
    MultilineTextView view(testFont);
    view.setText("one\ntwo\nthree");
    view.setLineHeight(30);
    view.setMaxLines(2);
    f.frame(view, SDL_Rect{0, 0, 200, 200});
    CHECK_EQ_SZ(view.getSize().h, 60);
    CHECK(f.surfaceHasPixels(SDL_Rect{0, 30, 200, 30}));
    CHECK(!f.surfaceHasPixels(SDL_Rect{0, 60, 200, 30}));
}

TEST(test_multiline_cut_shows_the_lines_that_fit) {
    Fixture f(kScreenW, 200);
    MultilineTextView view(testFont);
    view.setText("one\ntwo\nthree");
    view.setLineHeight(30);
    view.setCutToSpace(true);
    f.frame(view, SDL_Rect{0, 0, 200, 70});
    CHECK_EQ_SZ(view.getSize().h, 60);
}

TEST(test_multiline_without_a_cut_shows_each_line) {
    Fixture f(kScreenW, 200);
    MultilineTextView view(testFont);
    view.setText("one\ntwo\nthree");
    view.setLineHeight(30);
    UiLayoutContext& root = f.compositor.beginLayout(f.context);
    CHECK_EQ_SZ(root.layoutChild(view, SDL_Rect{0, 0, 200, 70}).getSize().h, 90);
}

// The dialog makes the space of its content as high as the layout of the content
TEST(test_dialog_takes_the_height_of_its_content) {
    Fixture f(kScreenW, 200);
    DialogView dialog(200, DialogView::Alignment::Left, testFont, testFont);
    MultilineTextView message(testFont);
    dialog.setTitle("Title");
    dialog.setContent(&message);
    message.setText("one\ntwo\nthree");
    f.frame(dialog, SDL_Rect{0, 0, kScreenW, 200});
    CHECK(message.getSize().h == getMultilineHeight(3));
}


namespace {

// A view that counts its attaches and its draws, and posts a task on demand
class AttachProbeView final : public UiView {
public:
    void layout(UiLayoutContext& context) override { context.setSize(UiSize{4, 4}); }
    void draw(UiDrawContext&) override { draws++; }
    bool isAttached() const { return hasContext(); }
    TaskId postTask() { return getScheduler().postDelayed([] {}, 1000); }
    bool isPending(TaskId task) { return getScheduler().isTaskPending(task); }

    int attaches = 0;
    int draws    = 0;

protected:
    void onAttach(UiContext&) override { attaches++; }
};

// A composite view that lays out its child, or only measures it
class AttachParentView final : public UiLayout {
public:
    explicit AttachParentView(UiView& child) : child(child) {}
    void layout(UiLayoutContext& context) override {
        if (measureOnly) {
            context.createMeasuringContext().layoutChild(child, context.getMaxRect());
        } else {
            context.layoutChild(child, context.getMaxRect());
        }
    }
    bool measureOnly = false;

private:
    UiView& child;
};

}  // namespace

const SDL_Rect kProbe = {0, 0, 4, 4};

// Lays out `view` as the layout of a page: the views that the layout does not reach detach
void pageFrame(Fixture& f, UiView& view, bool layOut = true) {
    f.context.startLayout();
    UiLayoutContext& root = f.compositor.beginLayout(f.context);
    if (layOut) root.layoutChild(view, kProbe);
    f.context.finishLayout();
    f.compositor.drawFrame();
}

TEST(test_child_attaches_at_its_first_layout_with_no_forward) {
    Fixture f;
    AttachProbeView child;
    AttachParentView parent(child);
    CHECK(!child.isAttached());
    pageFrame(f, parent);
    CHECK(child.isAttached());
    CHECK_EQ_INT(child.attaches, 1);
}

TEST(test_measure_attaches_the_view) {
    Fixture f;
    AttachProbeView child;
    AttachParentView parent(child);
    parent.measureOnly = true;
    pageFrame(f, parent);
    CHECK(child.isAttached());
    pageFrame(f, parent);
    CHECK(child.isAttached());
    CHECK_EQ_INT(child.attaches, 1);
}

TEST(test_attach_runs_one_time_over_several_frames) {
    Fixture f;
    AttachProbeView view;
    for (int frame = 0; frame < 5; frame++) pageFrame(f, view);
    CHECK_EQ_INT(view.attaches, 1);
}

TEST(test_attached_view_draws_again) {
    Fixture f;
    AttachProbeView view;
    pageFrame(f, view);
    CHECK_EQ_INT(view.draws, 1);
    pageFrame(f, view);
    CHECK_EQ_INT(view.draws, 1);
    f.context.setScreen(f.compositor);
    pageFrame(f, view);
    CHECK_EQ_INT(view.attaches, 2);
    CHECK_EQ_INT(view.draws, 2);
}

TEST(test_view_that_leaves_the_page_layout_detaches_and_loses_its_tasks) {
    Fixture f;
    AttachProbeView view;
    pageFrame(f, view);
    const TaskId task = view.postTask();
    CHECK(view.isPending(task));
    pageFrame(f, view, false);
    CHECK(!view.isAttached());
    CHECK(!f.context.isTaskPending(task));
    pageFrame(f, view);
    CHECK(view.isAttached());
    CHECK_EQ_INT(view.attaches, 2);
}

TEST(test_layout_outside_a_page_layout_detaches_no_view) {
    Fixture f;
    AttachProbeView view;
    AttachProbeView other;
    pageFrame(f, view);
    // A snapshot lays out a covered page with no start and no finish
    f.compositor.beginLayout(f.context).layoutChild(other, kProbe);
    f.compositor.drawFrame();
    CHECK(view.isAttached());
}

TEST(test_new_screen_attaches_again_with_no_detach) {
    Fixture f;
    AttachProbeView view;
    pageFrame(f, view);
    const TaskId task = view.postTask();
    f.context.setScreen(f.compositor);
    pageFrame(f, view);
    CHECK_EQ_INT(view.attaches, 2);
    CHECK(view.isPending(task));
}

TEST(test_view_that_moves_to_another_context_loses_its_tasks) {
    Fixture f;
    std::unique_ptr<PageContext> other = PageContext::create(f.compositor);
    AttachProbeView view;
    pageFrame(f, view);
    const TaskId task = view.postTask();
    f.compositor.beginLayout(*other).layoutChild(view, kProbe);
    CHECK(!f.context.isTaskPending(task));
    CHECK_EQ_INT(view.attaches, 2);
    // The old context no longer holds the view, thus its next page layout does not detach it
    pageFrame(f, view, false);
    CHECK(view.isAttached());
}

TEST(test_destroyed_view_leaves_its_context) {
    Fixture f;
    TaskId task = 0;
    {
        AttachProbeView view;
        pageFrame(f, view);
        task = view.postTask();
    }
    CHECK(!f.context.isTaskPending(task));
    // The context no longer holds the view, thus its next page layout reads nothing of it
    AttachProbeView next;
    pageFrame(f, next);
    CHECK(next.isAttached());
}

TEST(test_destroyed_context_detaches_its_views) {
    Fixture f;
    std::unique_ptr<PageContext> other = PageContext::create(f.compositor);
    AttachProbeView view;
    f.compositor.beginLayout(*other).layoutChild(view, kProbe);
    CHECK(view.isAttached());
    other.reset();
    CHECK(!view.isAttached());
}

int main() {
    SDL_Init(0);
    if (TTF_Init() != 0) {
        printf("SKIP no TTF: %s\n", TTF_GetError());
        return 0;
    }
    testFont = TTF_OpenFont("../NextUI/workspace/all/show2/RoundedMplus1c-Bold-reduced.ttf", 18);
    if (!testFont) {
        printf("SKIP no font: %s\n", TTF_GetError());
        return 0;
    }
    SDL_Surface* themeScreen = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
    Theme_init(themeScreen);
    RUN(test_text_that_fits_is_not_clipped_and_does_not_scroll);
    RUN(test_scroll_moves_at_its_speed_on_the_active_time);
    RUN(test_stop_keeps_the_offset_and_a_new_speed_continues_from_it);
    RUN(test_a_new_speed_in_a_frame_does_not_count_the_time_before_it);
    RUN(test_a_start_before_the_first_layout_scrolls_after_the_attach);
    RUN(test_a_text_that_leaves_the_page_layout_stops_until_the_next_start);
    RUN(test_a_text_that_leaves_the_layout_stops_until_the_next_start);
    RUN(test_reset_puts_the_text_at_its_start_and_keeps_the_speed);
    RUN(test_new_text_puts_the_offset_at_zero_and_keeps_the_speed);
    RUN(test_text_that_fits_does_not_move);
    RUN(test_start_moves_the_text_from_the_next_frame);
    RUN(test_row_forwards_the_scroll_to_its_whole_text);
    RUN(test_row_scrolls_again_when_its_whole_text_comes_back);
    RUN(test_text_is_as_high_as_a_line_with_the_text_in_the_middle);
    RUN(test_text_takes_no_less_than_the_font_height);
    RUN(test_long_text_with_ellipsis_keeps_one_width);
    RUN(test_color_change_draws_the_view_again);
    RUN(test_scrolling_text_moves_to_the_layer_and_back);
    RUN(test_same_offset_in_whole_pixels_is_not_dirty);
    RUN(test_surface_only_mode_shows_the_text_at_its_offset);
    RUN(test_setter_with_the_same_value_is_not_dirty);
    RUN(test_transparent_box_has_no_content_until_it_gets_a_color);
    RUN(test_edge_pixel_on_the_surface_is_premultiplied);
    RUN(test_title_fades_at_the_cut_and_not_when_it_fits);
    RUN(test_row_shows_long_text_only_when_selected);
    RUN(test_selection_change_makes_the_row_dirty);
    RUN(test_cursor_move_draws_only_the_two_changed_rows);
    RUN(test_move_selects_the_new_row_and_clears_the_old_row);
    RUN(test_page_move_selects_the_new_row);
    RUN(test_data_change_selects_only_the_row_of_the_cursor);
    RUN(test_removed_item_moves_the_rows_with_no_draw);
    RUN(test_move_past_the_last_visible_row_scrolls);
    RUN(test_row_that_the_clip_cuts_is_hidden);
    RUN(test_page_needs_the_layout_after_a_data_change);
    RUN(test_move_past_an_end_wraps_to_the_other_end);
    RUN(test_single_item_does_not_move);
    RUN(test_empty_list_gives_minus_one);
    RUN(test_dialog_draws_again_only_for_a_change);
    RUN(test_multiline_text_wraps_at_a_space);
    RUN(test_multiline_text_starts_a_line_at_a_line_break);
    RUN(test_multiline_word_wrap_moves_a_long_word_to_the_next_line);
    RUN(test_multiline_character_wrap_fills_the_line);
    RUN(test_multiline_long_word_breaks_at_a_character);
    RUN(test_multiline_spans_share_a_line);
    RUN(test_multiline_same_text_is_not_dirty);
    RUN(test_multiline_fixed_line_height_sets_the_height);
    RUN(test_multiline_max_lines_shows_the_first_lines);
    RUN(test_multiline_cut_shows_the_lines_that_fit);
    RUN(test_multiline_without_a_cut_shows_each_line);
    RUN(test_dialog_takes_the_height_of_its_content);
    RUN(test_dialog_pages_give_the_earphone_buttons_to_the_playback);
    RUN(test_controls_help_ignores_start_with_a_volume_button);
    RUN(test_quit_dialog_ignores_start_with_a_volume_button);
    RUN(test_confirm_dialog_gives_yes_on_a_and_no_on_b);
    RUN(test_database_error_dialog_closes_on_a_pad_button);
    RUN(test_database_error_dialog_stays_open_at_a_media_key);
    RUN(test_database_error_dialog_ignores_a_press_from_before_the_reset);
    RUN(test_status_group_refresh_with_the_same_pixels_changes_nothing);
    RUN(test_status_group_refresh_gives_the_same_pixels);
    RUN(test_status_group_renders_when_a_value_settles);
    RUN(test_status_group_renders_again_after_a_change_during_the_render);
    RUN(test_status_group_ignores_a_value_that_comes_back);
    RUN(test_shorter_status_group_leaves_no_old_pixels);
    RUN(test_right_view_shows_at_the_right_edge_of_the_row);
    RUN(test_long_text_ends_before_the_right_view);
    RUN(test_right_view_set_after_the_first_layout_attaches_at_the_next_layout);
    RUN(test_removed_right_view_makes_the_row_dirty);
    RUN(test_new_right_view_of_a_selected_row_draws_the_row_again);
    RUN(test_up_indicator_shows_over_the_title_with_no_title_draw);
    RUN(test_new_title_draws_the_title_under_the_indicator);
    RUN(test_scrolling_title_frame_changes_the_layer_only);
    RUN(test_hints_change_keeps_the_down_indicator_over_them);
    RUN(test_new_status_width_moves_the_title);
    RUN(test_screen_content_gets_the_body_inside_its_margins);
    RUN(test_content_with_no_scroll_has_no_scroll_indicators);
    RUN(test_scrolling_content_gets_the_scroll_indicators);
    RUN(test_list_tells_where_it_can_scroll);
    RUN(test_empty_list_cannot_scroll);
    RUN(test_scroll_view_tells_where_it_can_scroll);
    RUN(test_list_screen_lays_out_the_list_inside_the_side_margins);
    RUN(test_empty_state_replaces_the_list_and_the_indicators);
    RUN(test_bitmap_is_scaled_into_the_view);
    RUN(test_bitmap_view_frees_its_bitmap);
    RUN(test_bitmap_view_loads_an_image_from_memory);
    RUN(test_bitmap_view_does_not_load_a_missing_file);
    RUN(test_progress_bar_fills_its_percent);
    RUN(test_progress_bar_clamps_a_value_out_of_the_range);
    RUN(test_child_attaches_at_its_first_layout_with_no_forward);
    RUN(test_measure_attaches_the_view);
    RUN(test_attach_runs_one_time_over_several_frames);
    RUN(test_attached_view_draws_again);
    RUN(test_view_that_leaves_the_page_layout_detaches_and_loses_its_tasks);
    RUN(test_layout_outside_a_page_layout_detaches_no_view);
    RUN(test_new_screen_attaches_again_with_no_detach);
    RUN(test_view_that_moves_to_another_context_loses_its_tasks);
    RUN(test_destroyed_view_leaves_its_context);
    RUN(test_destroyed_context_detaches_its_views);
    return test_summary();
}
