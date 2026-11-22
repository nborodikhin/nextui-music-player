// Tests for src/ui/page/list_screen_controller.cpp - the cursor buttons of a list screen, the call of
// Left and Right to the page, and the selection that the page knows.
//
// The layer is a test double of ui_layers.c, as in test_ui_views.cpp.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "test.h"
#include "ui/page/list_screen_controller.hpp"
#include "ui/page/page_context.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/view/ui_compositor.hpp"
#include "ui_layers.h"
#include "ui_utils.h"

extern "C" void UiLayer_clear(UiLayer) {}
extern "C" void UiLayer_blit(SDL_Surface*, int, int, UiLayer) {}

using namespace input;

namespace {

TTF_Font* testFont = nullptr;

// An item of 10 pixels
class TestItem : public ListItemView {
public:
    void layout(UiLayoutContext& context) override { context.setSize(UiSize{context.getMaxSize().w, 10}); }
    bool isDirty() const override { return false; }
    void draw(UiDrawContext&) override {}
    // A row that loses the selection stops its text, as ListRowView does
    void setSelected(bool value) override {
        selected = value;
        if (!value) scrolling = false;
    }
    void startScrolling() override { scrolling = true; }
    void stopScrolling() override { scrolling = false; }
    void resetScrolling() override { resets++; }

    bool selected  = false;
    bool scrolling = false;
    int resets     = 0;
};

class TestAdapter : public ListAdapter {
public:
    std::vector<long> ids;
    int getCount() const override { return static_cast<int>(ids.size()); }
    long getItemId(int index) const override { return ids[static_cast<size_t>(index)]; }
    std::shared_ptr<ListItemView> getView(int) override { return std::make_shared<TestItem>(); }
};

class TestListener : public ListScreenListener {
public:
    bool onLeftRight(int listItemPosition, HorizontalDirection direction, bool isRepeat) override {
        leftRightCalls.push_back({listItemPosition, direction, isRepeat});
        return handleLeftRight;
    }
    void onSelectionChanged(int listItemPosition) override { selections.push_back(listItemPosition); }

    struct LeftRightCall {
        int position;
        HorizontalDirection direction;
        bool isRepeat;
    };
    bool handleLeftRight = false;
    std::vector<LeftRightCall> leftRightCalls;
    std::vector<int> selections;
};

// Six items of 10 pixels in a list of 35: three whole items show
const SDL_Rect kList = {0, 0, 100, 35};

struct Fixture {
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 200, 100, 32, SDL_PIXELFORMAT_ARGB8888);
    UiCompositor compositor;
    std::unique_ptr<PageContext> context = PageContext::create(compositor);
    OwnerScheduler scheduler{1};
    TestAdapter adapter;
    ListScreenView view{testFont};
    TestListener listener;
    ListScreenController controller{view, scheduler, listener};
    uint32_t elapsed = 0;

    explicit Fixture(std::vector<long> ids = {1, 2, 3, 4, 5, 6}) {
        compositor.setScreen(screen);
        scheduler.setContext(*context);
        adapter.ids = std::move(ids);
        list().setAdapter(adapter);
        layout();
        controller.start();
    }
    ~Fixture() { SDL_FreeSurface(screen); }

    ListView& list() { return view.getListView(); }
    TestItem& item(int index) { return static_cast<TestItem&>(list().getItemView(index)); }

    // Lays out the list alone, thus a page move knows the heights of the items
    void layout() {
        UiLayoutContext& root = compositor.beginLayout(*context);
        root.layoutChild(list(), kList);
        compositor.drawFrame();
    }

    void tick(uint32_t stepMs) {
        elapsed += stepMs;
        context->startFrame(elapsed, context->getActiveTime() + stepMs);
        context->runFrameTasks();
    }

    bool press(Button button) { return controller.onButtonEvent(ButtonEvent{0, event::ShortPress{button}}); }
    bool repeat(Button button) { return controller.onButtonEvent(ButtonEvent{0, event::Repeat{button}}); }
};

TEST(test_up_and_down_move_one_row_and_wrap) {
    Fixture f;
    CHECK(f.press(Button::DpadDown));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 1);
    CHECK(f.press(Button::AnalogUp));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 0);
    CHECK(f.press(Button::DpadUp));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 5);
    CHECK(f.press(Button::AnalogDown));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 0);
}

TEST(test_repeat_acts_as_a_press) {
    Fixture f;
    CHECK(f.repeat(Button::DpadDown));
    CHECK(f.repeat(Button::DpadDown));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 2);
}

TEST(test_left_and_right_go_to_the_page_with_the_selected_position) {
    Fixture f;
    f.press(Button::DpadDown);
    f.listener.handleLeftRight = true;
    CHECK(f.press(Button::DpadRight));
    CHECK(f.press(Button::AnalogLeft));
    CHECK_EQ_SZ(f.listener.leftRightCalls.size(), 2u);
    CHECK_EQ_INT(f.listener.leftRightCalls[0].position, 1);
    CHECK(f.listener.leftRightCalls[0].direction == HorizontalDirection::Right);
    CHECK(f.listener.leftRightCalls[1].direction == HorizontalDirection::Left);
    CHECK(!f.listener.leftRightCalls[0].isRepeat);
    // The page handled them, thus the cursor stays
    CHECK_EQ_INT(f.list().getSelectedIndex(), 1);
}

TEST(test_held_left_and_right_go_to_the_page_as_repeats) {
    Fixture f;
    f.listener.handleLeftRight = true;
    CHECK(f.repeat(Button::DpadRight));
    CHECK_EQ_SZ(f.listener.leftRightCalls.size(), 1u);
    CHECK(f.listener.leftRightCalls[0].isRepeat);
    CHECK_EQ_INT(f.list().getSelectedIndex(), 0);
}

TEST(test_left_and_right_that_the_page_does_not_handle_move_one_page) {
    Fixture f;
    CHECK(f.press(Button::DpadRight));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 3);
    CHECK(f.press(Button::DpadLeft));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 0);
    CHECK_EQ_SZ(f.listener.leftRightCalls.size(), 2u);
}

TEST(test_start_gives_the_first_selection) {
    Fixture f;
    CHECK_EQ_SZ(f.listener.selections.size(), 1u);
    CHECK_EQ_INT(f.listener.selections[0], 0);
    CHECK(f.item(0).selected);
}

TEST(test_move_gives_the_new_selection) {
    Fixture f;
    f.press(Button::DpadDown);
    CHECK_EQ_SZ(f.listener.selections.size(), 2u);
    CHECK_EQ_INT(f.listener.selections[1], 1);
}

TEST(test_move_starts_the_wait_before_the_new_row_scrolls) {
    Fixture f;
    f.press(Button::DpadDown);
    CHECK(!f.item(1).scrolling);
    f.tick(SCROLL_START_DELAY - 1);
    CHECK(!f.item(1).scrolling);
    f.tick(1);
    CHECK(f.item(1).scrolling);
    f.press(Button::DpadDown);
    f.tick(SCROLL_START_DELAY);
    CHECK(!f.item(1).scrolling);
    CHECK(f.item(2).scrolling);
}

TEST(test_data_change_gives_the_new_selection_when_the_selected_row_goes_away) {
    Fixture f({1, 2, 3});
    f.press(Button::DpadDown);
    f.adapter.ids = {1, 3};
    f.controller.notifyDataChanged();
    CHECK_EQ_SZ(f.listener.selections.size(), 3u);
    CHECK_EQ_INT(f.listener.selections[2], 1);
    CHECK_EQ_SZ(f.list().getItemId(1), 3u);
}

TEST(test_data_change_gives_the_new_position_when_a_row_comes_before_the_selection) {
    Fixture f({1, 2, 3});
    f.press(Button::DpadDown);
    f.adapter.ids = {9, 1, 2, 3};
    f.controller.notifyDataChanged();
    CHECK_EQ_SZ(f.listener.selections.size(), 3u);
    CHECK_EQ_INT(f.listener.selections[2], 2);
}

TEST(test_data_change_with_the_same_selection_gives_nothing) {
    Fixture f({1, 2, 3});
    f.adapter.ids = {1, 2, 3, 4};
    f.controller.notifyDataChanged();
    CHECK_EQ_SZ(f.listener.selections.size(), 1u);
}

TEST(test_other_buttons_are_not_handled) {
    Fixture f;
    CHECK(!f.press(Button::A));
    CHECK(!f.press(Button::B));
    CHECK(!f.press(Button::X));
    CHECK(!f.controller.onButtonEvent(ButtonEvent{0, event::LongPress{Button::DpadDown}}));
    CHECK_EQ_INT(f.list().getSelectedIndex(), 0);
    CHECK_EQ_SZ(f.listener.leftRightCalls.size(), 0u);
    CHECK_EQ_SZ(f.listener.selections.size(), 1u);
}

}  // namespace

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
    RUN(test_up_and_down_move_one_row_and_wrap);
    RUN(test_repeat_acts_as_a_press);
    RUN(test_left_and_right_go_to_the_page_with_the_selected_position);
    RUN(test_held_left_and_right_go_to_the_page_as_repeats);
    RUN(test_left_and_right_that_the_page_does_not_handle_move_one_page);
    RUN(test_start_gives_the_first_selection);
    RUN(test_move_gives_the_new_selection);
    RUN(test_move_starts_the_wait_before_the_new_row_scrolls);
    RUN(test_data_change_gives_the_new_selection_when_the_selected_row_goes_away);
    RUN(test_data_change_gives_the_new_position_when_a_row_comes_before_the_selection);
    RUN(test_data_change_with_the_same_selection_gives_nothing);
    RUN(test_other_buttons_are_not_handled);
    TTF_CloseFont(testFont);
    TTF_Quit();
    SDL_Quit();
    return test_summary();
}
