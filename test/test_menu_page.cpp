// Tests for src/ui/page/menu_page.cpp - the rows of a menu, their reload, and the views that show
// them.
//
// The layer is a test double of ui_layers.c, as in test_ui_views.cpp.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "test.h"
#include "ui/page/menu_page.hpp"
#include "ui/page/page_context.hpp"
#include "ui/view/ui_compositor.hpp"
#include "ui_fonts.h"
#include "ui_layers.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
#include "config.h"
}

extern "C" void UiLayer_clear(UiLayer) {}
extern "C" void UiLayer_blit(SDL_Surface*, int, int, UiLayer) {}

namespace {

/** A menu whose ids and labels a test changes */
class TestMenuAdapter final : public MenuAdapter {
public:
    std::vector<long> ids;
    std::map<long, std::string> labels;
    std::map<long, std::string> secondaryTexts;

protected:
    std::vector<long> loadRowIds() const override { return ids; }
    MenuRow loadRow(long id) const override {
        MenuRow row;
        row.id    = id;
        row.label = labels.count(id) ? labels.at(id) : "row";
        if (secondaryTexts.count(id)) row.secondaryText = secondaryTexts.at(id);
        return row;
    }
};

class TestMenuListener final : public MenuAdapterListener {
public:
    void onRowsChanged() override { rowsChanged++; }
    void onRowChanged(int index) override { changedRows.push_back(index); }

    int rowsChanged = 0;
    std::vector<int> changedRows;
};

struct MenuFixture {
    TestMenuAdapter adapter;
    TestMenuListener listener;

    MenuFixture() {
        adapter.ids = {1, 2, 3};
        adapter.reload();
        adapter.setListener(&listener);
        // The list asks for each view that it shows
        for (int index = 0; index < adapter.getCount(); index++) adapter.getView(index);
    }
};

constexpr int kScreenW = 640;

struct DrawFixture {
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, SCALE1(PILL_SIZE), 32, SDL_PIXELFORMAT_ARGB8888);
    UiCompositor compositor;
    std::unique_ptr<PageContext> context = PageContext::create(compositor);

    DrawFixture() { compositor.setScreen(screen); }
    ~DrawFixture() { SDL_FreeSurface(screen); }

    void frame(UiView& view) {
        compositor.beginLayout(*context).layoutChild(view, SDL_Rect{0, 0, kScreenW, SCALE1(PILL_SIZE)});
        compositor.drawFrame();
    }

    /**
     * Returns the left edge of the text in the right half of the row: the first column with a
     * pixel that is not the pixel at the top of that column. A band and the background are the
     * same in each row of a column, and the text is not at the top.
     */
    int getRightTextLeft() const {
        for (int x = kScreenW / 2; x < kScreenW; x++) {
            const uint32_t top = pixelAt(x, 0);
            for (int y = 1; y < screen->h; y++) {
                if (pixelAt(x, y) != top) return x;
            }
        }
        return kScreenW;
    }

    uint32_t pixelAt(int x, int y) const {
        return reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(screen->pixels) + y * screen->pitch)[x];
    }

    // Returns true where a pixel of the right quarter of the row is not transparent
    bool rightHasPixels() const {
        for (int y = 0; y < screen->h; y++) {
            const uint32_t* line = reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(screen->pixels) + y * screen->pitch);
            for (int x = kScreenW * 3 / 4; x < kScreenW; x++) {
                if (line[x] >> 24) return true;
            }
        }
        return false;
    }
};

}  // namespace

TEST(test_reload_with_new_ids_reads_the_items_again) {
    MenuFixture f;
    f.adapter.ids = {1, 4, 2, 3};
    f.adapter.reload();
    CHECK_EQ_INT(f.listener.rowsChanged, 1);
    CHECK_EQ_INT(f.adapter.getCount(), 4);
    CHECK_EQ_INT(static_cast<int>(f.adapter.getItemId(1)), 4);
}

TEST(test_reload_of_one_row_changes_that_row_only) {
    MenuFixture f;
    f.adapter.labels[2] = "new label";
    f.adapter.reload(2);
    CHECK_EQ_INT(f.listener.rowsChanged, 0);
    CHECK_EQ_INT(static_cast<int>(f.listener.changedRows.size()), 1);
    CHECK_EQ_INT(f.listener.changedRows[0], 1);
    CHECK(f.adapter.getRow(1).label == "new label");
}

TEST(test_reload_with_the_same_texts_tells_nothing) {
    MenuFixture f;
    f.adapter.reload();
    f.adapter.reload(3);
    CHECK_EQ_INT(f.listener.rowsChanged, 0);
    CHECK(f.listener.changedRows.empty());
}

TEST(test_reload_tells_each_row_whose_text_changed) {
    MenuFixture f;
    f.adapter.labels[3] = "other";
    f.adapter.reload();
    CHECK_EQ_INT(f.listener.rowsChanged, 0);
    CHECK_EQ_INT(static_cast<int>(f.listener.changedRows.size()), 1);
    CHECK_EQ_INT(f.listener.changedRows[0], 2);
}

TEST(test_row_keeps_its_view_while_its_id_stays) {
    MenuFixture f;
    const std::shared_ptr<ListItemView> before = f.adapter.getView(1);
    f.adapter.ids = {5, 1, 2, 3};
    f.adapter.reload();
    CHECK(f.adapter.getView(2) == before);
}

TEST(test_secondary_text_shows_at_the_right_of_the_row) {
    TestMenuAdapter adapter;
    adapter.ids               = {1, 2};
    adapter.labels[1]         = "a";
    adapter.labels[2]         = "a";
    adapter.secondaryTexts[2] = "Pop";
    adapter.reload();

    DrawFixture plain;
    plain.frame(*adapter.getView(0));
    CHECK(!plain.rightHasPixels());

    DrawFixture withText;
    withText.frame(*adapter.getView(1));
    CHECK(withText.rightHasPixels());
}

/** A menu with one row that has a value */
class ValueMenuAdapter final : public MenuAdapter {
protected:
    std::vector<long> loadRowIds() const override { return {1}; }
    MenuRow loadRow(long id) const override {
        MenuRow row;
        row.id       = id;
        row.label    = "a";
        row.value    = "Off";
        row.onChange = [](HorizontalDirection) {};
        return row;
    }
};

TEST(test_value_shows_its_arrows_only_while_the_row_is_selected) {
    ValueMenuAdapter adapter;
    adapter.reload();
    std::shared_ptr<ListItemView> row = adapter.getView(0);

    DrawFixture plain;
    plain.frame(*row);
    const int plainLeft = plain.getRightTextLeft();

    row->setSelected(true);
    DrawFixture selected;
    selected.frame(*row);
    // "< Off >" starts left of "Off", and both end at the right padding
    CHECK(selected.getRightTextLeft() < plainLeft);

    row->setSelected(false);
    DrawFixture again;
    again.frame(*row);
    CHECK_EQ_INT(again.getRightTextLeft(), plainLeft);
}

int main() {
    SDL_Init(0);
    if (TTF_Init() != 0) {
        printf("SKIP no TTF: %s\n", TTF_GetError());
        return 0;
    }
    // The rows of a menu pick the fonts of the app
    CFG_setFontFile(nullptr);
    Fonts_load();
    if (!Fonts_getLarge()) {
        printf("SKIP no font of the app at %s\n", RES_PATH);
        return 0;
    }
    // The text of a row takes a color of the theme
    SDL_Surface* themeScreen = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
    Theme_init(themeScreen);
    RUN(test_reload_with_new_ids_reads_the_items_again);
    RUN(test_reload_of_one_row_changes_that_row_only);
    RUN(test_reload_with_the_same_texts_tells_nothing);
    RUN(test_reload_tells_each_row_whose_text_changed);
    RUN(test_row_keeps_its_view_while_its_id_stays);
    RUN(test_secondary_text_shows_at_the_right_of_the_row);
    RUN(test_value_shows_its_arrows_only_while_the_row_is_selected);
    SDL_FreeSurface(themeScreen);
    Fonts_unload();
    TTF_Quit();
    SDL_Quit();
    return test_summary();
}
