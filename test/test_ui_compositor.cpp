// Tests for src/ui/view/ui_compositor.cpp and src/ui/view/ui_view.cpp - the display list of a frame,
// the damage, the draw of the views, and the composition on the surface and on the layer.
//
// The layers are a test double of ui_layers.c: a buffer of the screen size for each layer, that a
// blit replaces, as PLAT_drawOnLayer() does.

#include <cstdint>
#include <vector>

#include "test.h"
#include "ui/view/ui_compositor.hpp"
#include "ui/view/ui_layout.hpp"
#include "ui_layers.h"

namespace {

constexpr int kScreenW = 40;
constexpr int kScreenH = 20;

constexpr int kLayerCount = UI_LAYER_TOAST + 1;

std::vector<std::vector<uint32_t>> layers(kLayerCount, std::vector<uint32_t>(kScreenW * kScreenH, 0));
std::vector<uint32_t>& layerPixels = layers[UI_LAYER_ANIMATION];
int layerBlits                     = 0;

}  // namespace

extern "C" void UiLayer_clear(UiLayer layer) { layers[layer].assign(layers[layer].size(), 0); }

extern "C" void UiLayer_blit(SDL_Surface* surface, int x, int y, UiLayer layer) {
    if (layer == UI_LAYER_ANIMATION) layerBlits++;
    for (int row = 0; row < surface->h; row++) {
        const uint32_t* from = reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(surface->pixels) +
                                                                 row * surface->pitch);
        for (int column = 0; column < surface->w; column++) {
            layers[layer][(y + row) * kScreenW + x + column] = from[column];
        }
    }
}

namespace {

// A view that fills its whole rectangle with one premultiplied color. Its content rectangle is
// the default one, or none.
class TestView : public UiView {
public:
    explicit TestView(uint32_t color) : color(color) {}

    void layout(UiLayoutContext& context) override {
        context.setTargetLayer(target);
    }
    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override {
        dirty = false;
        draws++;
        if (noContent) return;
        SDL_FillRect(context.getSurface(), nullptr, color);
        if (content.w > 0) context.setContentRect(content);
    }

    uint32_t color;
    UiTargetLayer target = UiTargetLayer::MainSurface;
    bool dirty           = false;
    bool noContent       = false;
    SDL_Rect content     = {0, 0, 0, 0};
    int draws            = 0;
};

// A view that only holds one child.
class TestParent : public UiLayout {
public:
    explicit TestParent(UiView& child) : child(child) {}
    void layout(UiLayoutContext& context) override {
        context.layoutChild(child, SDL_Rect{0, 0, context.getSize().w, context.getSize().h});
    }

private:
    UiView& child;
};

constexpr uint32_t kRed   = 0xFFFF0000;
constexpr uint32_t kGreen = 0xFF00FF00;

struct Fixture {
    SDL_Surface* screen;
    UiCompositor compositor;

    Fixture() {
        screen = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_FillRect(screen, nullptr, 0x12345678);
        for (std::vector<uint32_t>& pixels : layers) pixels.assign(pixels.size(), 0);
        layerBlits = 0;
        compositor.setScreen(screen);
    }
    ~Fixture() { SDL_FreeSurface(screen); }

    uint32_t surfaceAt(int x, int y) const {
        return reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(screen->pixels) + y * screen->pitch)[x];
    }
    uint32_t layerAt(int x, int y) const { return layerPixels[y * kScreenW + x]; }
    uint32_t layerAt(UiLayer layer, int x, int y) const { return layers[layer][y * kScreenW + x]; }

    // Writes a marker into the surface, thus a test sees which pixels a frame composed
    void markSurface() { SDL_FillRect(screen, nullptr, 0x11111111); }
};

TEST(test_new_view_draws_and_composes) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{2, 3, 4, 5});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 1);
    CHECK(f.compositor.hasSurfaceChanged());
    CHECK_EQ_SZ(f.surfaceAt(2, 3), kRed);
    CHECK_EQ_SZ(f.surfaceAt(5, 7), kRed);
    CHECK_EQ_SZ(f.surfaceAt(6, 3), 0);
}

TEST(test_view_with_no_content_draws_nothing) {
    Fixture f;
    TestView view(kRed);
    view.noContent = true;
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{2, 3, 4, 5});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 1);
    CHECK(f.surfaceAt(2, 3) != kRed);

    // A view with no content is not drawn again while it does not change
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{2, 3, 4, 5});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 1);
}

TEST(test_smaller_content_composes_the_part_it_left) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(3, 1), kRed);

    view.content = SDL_Rect{0, 0, 2, 4};
    view.dirty   = true;
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_SZ(f.surfaceAt(3, 1), 0);
}

TEST(test_frame_with_no_change_composes_nothing) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{2, 3, 4, 5});
    f.compositor.drawFrame();
    f.markSurface();
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{2, 3, 4, 5});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 1);
    CHECK(!f.compositor.hasSurfaceChanged());
    CHECK(!f.compositor.hasLayersChanged());
    CHECK_EQ_SZ(f.surfaceAt(2, 3), 0x11111111);
}

TEST(test_removed_view_composes_what_is_under_it) {
    Fixture f;
    TestView under(kGreen);
    TestView over(kRed);
    UiLayoutContext& root = f.compositor.beginLayout();
    root.layoutChild(under, SDL_Rect{0, 0, 10, 10});
    root.layoutChild(over, SDL_Rect{2, 2, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(3, 3), kRed);

    f.compositor.beginLayout().layoutChild(under, SDL_Rect{0, 0, 10, 10});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(3, 3), kGreen);
    CHECK_EQ_INT(under.draws, 1);
    CHECK_EQ_INT(over.draws, 1);
}

TEST(test_moved_view_composes_old_and_new_place_with_no_draw) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{10, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 1);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0);
    CHECK_EQ_SZ(f.surfaceAt(11, 1), kRed);
}

TEST(test_resized_view_draws_again) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 6, 4});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 2);
    CHECK_EQ_SZ(f.surfaceAt(5, 1), kRed);
}

TEST(test_view_moved_to_layer_composes_both_targets) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    view.target = UiTargetLayer::AnimationLayer;
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK(f.compositor.hasSurfaceChanged());
    CHECK(f.compositor.hasLayersChanged());
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0);
    CHECK_EQ_SZ(f.layerAt(1, 1), kRed);
    CHECK_EQ_INT(view.draws, 1);
}

TEST(test_dirty_view_draws_and_composes) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    view.color = kGreen;
    view.dirty = true;
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 2);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kGreen);
}

TEST(test_overlap_composes_in_z_order_with_no_draw) {
    Fixture f;
    TestView under(kGreen);
    TestView over(kRed);
    UiLayoutContext& root = f.compositor.beginLayout();
    root.layoutChild(under, SDL_Rect{0, 0, 10, 10});
    root.layoutChild(over, SDL_Rect{2, 2, 4, 4});
    f.compositor.drawFrame();

    UiLayoutContext& next = f.compositor.beginLayout();
    next.layoutChild(under, SDL_Rect{0, 0, 10, 10});
    next.layoutChild(over, SDL_Rect{4, 2, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(2, 2), kGreen);
    CHECK_EQ_SZ(f.surfaceAt(5, 2), kRed);
    CHECK_EQ_INT(under.draws, 1);
    CHECK_EQ_INT(over.draws, 1);
}

TEST(test_hidden_views_compose_as_removed) {
    Fixture f;
    TestView view(kRed);
    TestParent parent(view);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();

    // The visibility of the owner
    view.setVisible(false);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0);
    view.setVisible(true);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);

    // The visibility of the layout
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4}).setVisible(false);
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0);

    // A hidden parent
    f.compositor.beginLayout().layoutChild(parent, SDL_Rect{0, 0, 4, 4}).setVisible(false);
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0);
    f.compositor.beginLayout().layoutChild(parent, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_INT(view.draws, 1);
}

TEST(test_entry_outside_its_clip_composes_nothing) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4}, SDL_Rect{10, 10, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 0);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0);
}

TEST(test_changed_clip_composes_the_part_it_shows_or_hides) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 8, 4}, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_SZ(f.surfaceAt(6, 1), 0);

    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 8, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(6, 1), kRed);

    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 8, 4}, SDL_Rect{0, 0, 2, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_SZ(f.surfaceAt(3, 1), 0);
    CHECK_EQ_INT(view.draws, 1);
}

TEST(test_view_that_moves_under_a_fixed_clip_composes_again) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 8, 4}, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    f.markSurface();
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{-2, 0, 8, 4}, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK(f.compositor.hasSurfaceChanged());
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_INT(view.draws, 1);
}

TEST(test_measuring_context_adds_no_entry) {
    Fixture f;
    TestView view(kRed);
    UiLayoutContext& root  = f.compositor.beginLayout();
    UiLayoutContext measuring = root.createMeasuringContext();
    CHECK_EQ_INT(measuring.layoutChild(view, SDL_Rect{0, 0, 4, 3}).getSize().h, 3);
    CHECK_EQ_INT(view.getSize().w, 4);
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 0);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0);
}

TEST(test_invalidate_all_composes_everything_with_no_draw) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    f.markSurface();
    f.compositor.invalidateAll();
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 1);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_SZ(f.surfaceAt(20, 10), 0);
}

TEST(test_surface_only_mode_puts_each_entry_on_the_surface) {
    Fixture f;
    TestView view(kRed);
    view.target = UiTargetLayer::AnimationLayer;
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.layerAt(1, 1), kRed);

    f.compositor.setSurfaceOnly(true);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_SZ(f.layerAt(1, 1), 0);
}

TEST(test_partial_alpha_is_premultiplied_on_layer_and_on_surface) {
    Fixture f;
    // Gray 0x80 at half alpha, premultiplied
    constexpr uint32_t kHalfGray = 0x80404040;
    TestView onSurface(kHalfGray);
    TestView onLayer(kHalfGray);
    onLayer.target        = UiTargetLayer::AnimationLayer;
    UiLayoutContext& root = f.compositor.beginLayout();
    root.layoutChild(onSurface, SDL_Rect{0, 0, 4, 4});
    root.layoutChild(onLayer, SDL_Rect{10, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.layerAt(11, 1), kHalfGray);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kHalfGray);
}

TEST(test_partial_alpha_is_straight_on_status_and_toast_layers) {
    Fixture f;
    constexpr uint32_t kHalfGray = 0x80404040;
    TestView onStatus(kHalfGray);
    TestView onToast(kHalfGray);
    onStatus.target       = UiTargetLayer::StatusLayer;
    onToast.target        = UiTargetLayer::ToastLayer;
    UiLayoutContext& root = f.compositor.beginLayout();
    root.layoutChild(onStatus, SDL_Rect{0, 0, 4, 4});
    root.layoutChild(onToast, SDL_Rect{10, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK(f.compositor.hasLayersChanged());
    CHECK_EQ_SZ(f.layerAt(UI_LAYER_STATUS, 1, 1), 0x80808080);
    CHECK_EQ_SZ(f.layerAt(UI_LAYER_TOAST, 11, 1), 0x80808080);
}

TEST(test_partial_alpha_is_premultiplied_on_background_and_selection_layers) {
    Fixture f;
    constexpr uint32_t kHalfGray = 0x80404040;
    TestView onBackground(kHalfGray);
    TestView onSelection(kHalfGray);
    onBackground.target   = UiTargetLayer::BackgroundLayer;
    onSelection.target    = UiTargetLayer::SelectionLayer;
    UiLayoutContext& root = f.compositor.beginLayout();
    root.layoutChild(onBackground, SDL_Rect{0, 0, 4, 4});
    root.layoutChild(onSelection, SDL_Rect{10, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.layerAt(UI_LAYER_BACKGROUND, 1, 1), kHalfGray);
    CHECK_EQ_SZ(f.layerAt(UI_LAYER_SELECTION, 11, 1), kHalfGray);
}

TEST(test_invalidate_all_clears_each_layer_that_was_used) {
    Fixture f;
    TestView view(kRed);
    view.target = UiTargetLayer::ToastLayer;
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.layerAt(UI_LAYER_TOAST, 1, 1), kRed);

    f.compositor.invalidateAll();
    f.compositor.beginLayout();
    f.compositor.drawFrame();
    CHECK_EQ_SZ(f.layerAt(UI_LAYER_TOAST, 1, 1), 0);
}

}  // namespace

// Gray 0x80 at half alpha, straight
constexpr uint32_t kStraightHalfGray = 0x80808080;

TEST(test_dim_gives_a_transparent_pixel_the_premultiplied_color) {
    Fixture f;
    SDL_FillRect(f.screen, nullptr, 0);
    f.compositor.dimSurface(kStraightHalfGray);
    CHECK(f.compositor.hasSurfaceChanged());
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0x80404040);
}

TEST(test_dim_blends_over_opaque_and_semi_transparent_pixels) {
    Fixture f;
    SDL_FillRect(f.screen, nullptr, kRed);
    // Red at half alpha, premultiplied
    SDL_Rect half = {0, 0, 2, 2};
    SDL_FillRect(f.screen, &half, 0x80400000);
    f.compositor.dimSurface(kStraightHalfGray);
    CHECK_EQ_SZ(f.surfaceAt(5, 5), 0xFFBF4040);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), 0xC0604040);
}

TEST(test_frame_after_dim_composes_everything_with_no_draw) {
    Fixture f;
    TestView view(kRed);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    f.compositor.dimSurface(kStraightHalfGray);
    f.compositor.beginLayout().layoutChild(view, SDL_Rect{0, 0, 4, 4});
    f.compositor.drawFrame();
    CHECK_EQ_INT(view.draws, 1);
    CHECK_EQ_SZ(f.surfaceAt(1, 1), kRed);
    CHECK_EQ_SZ(f.surfaceAt(20, 10), 0);
}

int main() {
    SDL_Init(0);
    RUN(test_new_view_draws_and_composes);
    RUN(test_view_with_no_content_draws_nothing);
    RUN(test_smaller_content_composes_the_part_it_left);
    RUN(test_frame_with_no_change_composes_nothing);
    RUN(test_removed_view_composes_what_is_under_it);
    RUN(test_moved_view_composes_old_and_new_place_with_no_draw);
    RUN(test_resized_view_draws_again);
    RUN(test_view_moved_to_layer_composes_both_targets);
    RUN(test_dirty_view_draws_and_composes);
    RUN(test_overlap_composes_in_z_order_with_no_draw);
    RUN(test_hidden_views_compose_as_removed);
    RUN(test_entry_outside_its_clip_composes_nothing);
    RUN(test_changed_clip_composes_the_part_it_shows_or_hides);
    RUN(test_view_that_moves_under_a_fixed_clip_composes_again);
    RUN(test_measuring_context_adds_no_entry);
    RUN(test_invalidate_all_composes_everything_with_no_draw);
    RUN(test_surface_only_mode_puts_each_entry_on_the_surface);
    RUN(test_partial_alpha_is_premultiplied_on_layer_and_on_surface);
    RUN(test_partial_alpha_is_straight_on_status_and_toast_layers);
    RUN(test_partial_alpha_is_premultiplied_on_background_and_selection_layers);
    RUN(test_invalidate_all_clears_each_layer_that_was_used);
    RUN(test_dim_gives_a_transparent_pixel_the_premultiplied_color);
    RUN(test_dim_blends_over_opaque_and_semi_transparent_pixels);
    RUN(test_frame_after_dim_composes_everything_with_no_draw);
    return test_summary();
}
