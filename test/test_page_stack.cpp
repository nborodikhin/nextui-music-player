// Host tests of the page stack: the navigation commands, the life cycle of the pages, and the
// input of the top page. The stack runs with test pages and no display.

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "input/input_state.hpp"
#include "test.h"
#include "ui/legacy_module_page.hpp"
#include "ui/page_stack.hpp"
#include "ui/view/ui_compositor.hpp"
#include "ui/view/ui_view.hpp"
#include "ui_layers.h"

using namespace input;

// A test double of ui_layers.c: it records whether a layer holds pixels
namespace {
bool layerHasPixels[UI_LAYER_TOAST + 1] = {};
}  // namespace

extern "C" void UiLayer_clear(UiLayer layer) { layerHasPixels[layer] = false; }

extern "C" void UiLayer_blit(SDL_Surface*, int, int, UiLayer layer) { layerHasPixels[layer] = true; }

namespace {

std::vector<std::string> pageLog;
/** The name of the page of each root view that the stack lays out */
std::vector<std::string> layoutLog;

/** The root view of a test page. It fills its bitmap with its color. */
class TestRootView : public UiView {
public:
    TestRootView(std::string name, uint32_t color) : name(std::move(name)), color(color) {}

    void layout(UiLayoutContext&) override { layoutLog.push_back(name); }
    bool isDirty() const override { return dirty; }

    void draw(UiDrawContext& context) override {
        dirty = false;
        if (color == 0) return;
        if (SDL_Surface* surface = context.getSurface()) SDL_FillRect(surface, nullptr, color);
    }

private:
    std::string name;
    uint32_t color;
    bool dirty = true;
};

class FakePlaybackControl : public PlaybackControl {
public:
    std::vector<std::string> commands;

    void togglePlayback() override { commands.push_back("toggle"); }
    void skipTrack(bool next) override { commands.push_back(next ? "next" : "previous"); }
    void changeVolume(int step) override { commands.push_back(step > 0 ? "volume+" : "volume-"); }
};

class FakeContext : public UiContext {
public:
    SDL_PixelFormat* getPixelFormat() const override { return nullptr; }
    UiSize getScreenSize() const override { return UiSize{640, 480}; }
};

template <class Page>
class TestPageImpl;

/** A page with a result of type R. Its implementation writes each callback to pageLog. */
template <class R>
struct TestPage final : public UiPage {
    using Result = R;
    std::string name;
    PageKind kind = PageKind::FullScreen;
    HelpId helpId = HELP_NONE;
    /** The premultiplied ARGB color of the root view, or 0 for a view with no pixels */
    uint32_t color = 0;
    /** Called with the implementation at each short press that the page gets */
    std::function<void(TestPageImpl<TestPage>&, Button)> onShortPress;

    std::unique_ptr<UiPageImplBase> createPageImpl() const override {
        return std::make_unique<TestPageImpl<TestPage>>(*this);
    }
};

using PlainPage  = TestPage<std::monostate>;
using ResultPage = TestPage<int>;

template <class Page>
class TestPageImpl final : public UiPageImpl<Page> {
public:
    explicit TestPageImpl(const Page& page)
        : UiPageImpl<Page>(page.kind), page(page), root(page.name, page.color) {}

    HelpId getHelpId() const override { return page.helpId; }
    void setContext(UiContext&) override { add("setContext"); }
    void onEnter() override { add("enter"); }
    void onExit() override { add("exit"); }
    void pause(uint32_t) override { add("pause"); }
    void resume(uint32_t) override { add("resume"); }
    void layout(UiLayoutContext& context) override { context.layoutChild(root, context.getMaxRect()); }

    bool onRawEvent(const RawEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ButtonPressed>(&event.kind)) {
            add(std::string("pressed ") + getButtonName(typedEvent->button));
        }
        if (std::get_if<event::ButtonReset>(&event.kind)) add("reset");
        return false;
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            add(std::string("short ") + getButtonName(typedEvent->button));
            if (page.onShortPress) page.onShortPress(*this, typedEvent->button);
            return true;
        }
        return false;
    }

    PageNavigator& navigator() { return this->getNavigator(); }
    void doClose() { this->close(); }
    void doFinish(typename Page::Result result)
        requires(!std::is_same_v<typename Page::Result, std::monostate>)
    {
        this->finish(result);
    }

private:
    void add(const std::string& what) { pageLog.push_back(page.name + " " + what); }

    Page page;
    TestRootView root;
};

PlainPage makePage(const char* name, PageKind kind = PageKind::FullScreen) {
    PlainPage page;
    page.name = name;
    page.kind = kind;
    return page;
}

struct StackFixture {
    FakePlaybackControl playback;
    FakeContext context;
    PageStack stack{playback};
    uint32_t now = 0;

    explicit StackFixture(const PlainPage& bottom) {
        pageLog.clear();
        stack.start(bottom.createPageImpl(), context);
    }

    void send(const RawEvent& event) { stack.getRawInput().onRawEvent(event); }
    void press(Button button) { send({now, event::ButtonPressed{button}}); }
    void release(Button button) { send({now, event::ButtonReleased{button}}); }
    void media() { send({now, event::Media{MediaKey::PlayPause}}); }

    /** Ends the poll of this frame: the tick of the input and of the top page, and the commands */
    void endFrame() {
        stack.tickInput(now);
        stack.tickTop(now);
        stack.endFrame(now);
        stack.beginPoll();
        now += 16;
    }

    void tap(Button button) {
        press(button);
        release(button);
        endFrame();
    }
};

bool isLog(const std::vector<std::string>& want) {
    if (pageLog == want) return true;
    for (const std::string& line : pageLog) printf("      %s\n", line.c_str());
    return false;
}

bool hasLine(const std::string& line) {
    for (const std::string& entry : pageLog) {
        if (entry == line) return true;
    }
    return false;
}

/** A bottom page that opens a page with A and a dialog with X */
PlainPage makeOpener(const PlainPage& page, const PlainPage& dialog) {
    PlainPage opener      = makePage("main");
    opener.onShortPress = [page, dialog](TestPageImpl<PlainPage>& impl, Button button) {
        if (button == Button::A) impl.navigator().open(page);
        if (button == Button::X) impl.navigator().open(dialog);
    };
    return opener;
}

}  // namespace

TEST(new_page_gets_its_context_before_enter) {
    StackFixture f(makePage("main"));
    CHECK(isLog({"main setContext", "main enter"}));
}

TEST(open_puts_the_page_on_the_top_at_the_end_of_the_frame) {
    StackFixture f(makeOpener(makePage("next"), makePage("dialog", PageKind::Dialog)));
    pageLog.clear();
    f.press(Button::A);
    f.release(Button::A);
    CHECK(isLog({"main pressed A", "main short A", "main reset"}));
    f.endFrame();
    CHECK(isLog({"main pressed A", "main short A", "main reset", "main pause", "next setContext",
                 "next enter"}));
}

TEST(second_command_of_a_frame_is_ignored) {
    StackFixture f(makePage("main"));
    PlainPage first  = makePage("first");
    PlainPage second = makePage("second");
    f.stack.open(first);
    f.stack.open(second);
    f.endFrame();
    CHECK(hasLine("first enter"));
    CHECK(!hasLine("second enter"));
}

// A and B in one poll: the page opens the row at A and gets no B
TEST(events_after_the_first_command_reach_no_page) {
    PlainPage main = makeOpener(makePage("next"), makePage("dialog", PageKind::Dialog));
    StackFixture f(main);
    pageLog.clear();
    f.press(Button::A);
    f.release(Button::A);
    f.press(Button::B);
    f.release(Button::B);
    f.endFrame();
    CHECK(!hasLine("main pressed B"));
    CHECK(!hasLine("main short B"));
    CHECK(hasLine("next enter"));
}

// A long press of START queues the Quit dialog. The handler owns the gesture, and a Media event
// later in the poll gives no playback call.
TEST(events_after_a_command_reach_no_owner) {
    StackFixture f(makePage("main"));
    int quitOpens = 0;
    f.stack.setDialogOpeners(nullptr, [&] {
        quitOpens++;
        f.stack.open(makePage("quit", PageKind::Dialog));
    });
    f.press(Button::Start);
    f.endFrame();
    f.now = ButtonDetector::kLongPressMs;
    f.stack.tickInput(f.now);
    f.media();
    f.release(Button::Start);
    f.endFrame();
    CHECK_EQ_INT(quitOpens, 1);
    CHECK_EQ_SZ(f.playback.commands.size(), 0);
    CHECK(hasLine("quit enter"));
}

// In one poll: the release of START, then X, then Media. Only Controls Help opens.
TEST(release_of_start_then_x_in_one_poll_opens_only_the_help) {
    PlainPage main = makeOpener(makePage("next"), makePage("dialog", PageKind::Dialog));
    main.helpId    = HELP_MAIN_MENU;
    StackFixture f(main);
    int helpOpens = 0;
    f.stack.setDialogOpeners(
        [&](HelpId) {
            helpOpens++;
            f.stack.open(makePage("help", PageKind::Dialog));
        },
        nullptr);
    f.press(Button::Start);
    f.endFrame();
    pageLog.clear();
    f.release(Button::Start);
    f.press(Button::X);
    f.release(Button::X);
    f.media();
    f.endFrame();
    CHECK_EQ_INT(helpOpens, 1);
    CHECK(!hasLine("main pressed X"));
    CHECK(!hasLine("dialog enter"));
    CHECK(hasLine("help enter"));
    CHECK_EQ_SZ(f.playback.commands.size(), 0);
}

// The dialog gets no event of the gesture that opened it: not A while START is held, and not B
// while A is held after the release of START. A press after all buttons are released reaches it.
TEST(dialog_gets_no_event_of_the_opening_gesture) {
    StackFixture f(makePage("main"));
    f.stack.setDialogOpeners(nullptr, [&] { f.stack.open(makePage("quit", PageKind::Dialog)); });
    f.press(Button::Start);
    f.endFrame();
    f.now = ButtonDetector::kLongPressMs;
    f.endFrame();
    CHECK(hasLine("quit enter"));
    pageLog.clear();
    f.press(Button::A);
    f.endFrame();
    f.release(Button::Start);
    f.endFrame();
    f.press(Button::B);
    f.release(Button::B);
    f.endFrame();
    f.release(Button::A);
    f.endFrame();
    CHECK(isLog({}));
    f.tap(Button::A);
    CHECK(isLog({"quit pressed A", "quit short A"}));
}

TEST(close_of_a_page_with_a_result_gives_the_default) {
    StackFixture f(makePage("main"));
    std::vector<int> results;
    ResultPage page;
    page.name         = "dialog";
    page.kind         = PageKind::Dialog;
    page.onShortPress = [](TestPageImpl<ResultPage>& impl, Button button) {
        if (button == Button::B) impl.doClose();
        if (button == Button::A) impl.doFinish(7);
    };
    f.stack.openForResult(page, [&](const int& result) { results.push_back(result); });
    f.endFrame();
    f.tap(Button::B);
    f.stack.openForResult(page, [&](const int& result) { results.push_back(result); });
    f.endFrame();
    f.tap(Button::A);
    CHECK_EQ_SZ(results.size(), 2);
    CHECK_EQ_INT(results[0], 0);
    CHECK_EQ_INT(results[1], 7);
}

TEST(callback_runs_before_the_page_under_resumes) {
    StackFixture f(makePage("main"));
    ResultPage page;
    page.name         = "dialog";
    page.kind         = PageKind::Dialog;
    page.onShortPress = [](TestPageImpl<ResultPage>& impl, Button) { impl.doFinish(1); };
    f.stack.openForResult(page, [](const int&) { pageLog.push_back("callback"); });
    f.endFrame();
    pageLog.clear();
    f.tap(Button::A);
    CHECK(isLog({"dialog pressed A", "dialog short A", "dialog reset", "dialog exit", "callback",
                 "main resume"}));
}

TEST(callback_that_calls_exit_ends_the_stack) {
    StackFixture f(makePage("main"));
    ResultPage page;
    page.name         = "quit";
    page.kind         = PageKind::Dialog;
    page.onShortPress = [](TestPageImpl<ResultPage>& impl, Button) { impl.doFinish(1); };
    f.stack.openForResult(page, [&](const int&) { f.stack.exit(); });
    f.endFrame();
    f.tap(Button::A);
    CHECK(f.stack.isDone());
    CHECK(f.stack.takeQuitRequest());
}

TEST(open_then_exit_in_one_frame_opens_and_does_not_quit) {
    StackFixture f(makePage("main"));
    f.stack.open(makePage("next"));
    f.stack.exit();
    f.endFrame();
    CHECK(!f.stack.isDone());
    CHECK(!f.stack.takeQuitRequest());
    CHECK(hasLine("next enter"));
}

TEST(exit_then_open_in_one_frame_quits) {
    StackFixture f(makePage("main"));
    f.stack.exit();
    f.stack.open(makePage("next"));
    f.endFrame();
    CHECK(f.stack.isDone());
    CHECK(f.stack.takeQuitRequest());
    CHECK(!hasLine("next enter"));
}

TEST(dialog_cannot_open_a_page) {
    StackFixture f(makePage("main"));
    f.stack.open(makePage("dialog", PageKind::Dialog));
    f.endFrame();
    f.stack.open(makePage("next"));
    f.endFrame();
    CHECK(!hasLine("next enter"));
    CHECK(f.stack.isDialogOnTop());
}

TEST(covered_page_pauses_and_resumes) {
    StackFixture f(makePage("main"));
    PlainPage next    = makePage("next");
    next.onShortPress = [](TestPageImpl<PlainPage>& impl, Button) { impl.doClose(); };
    f.stack.open(next);
    f.endFrame();
    f.tap(Button::B);
    CHECK(isLog({"main setContext", "main enter", "main reset", "main pause", "next setContext",
                 "next enter", "next pressed B", "next short B", "next reset", "next exit",
                 "main resume"}));
}

TEST(exit_closes_each_page_from_the_top_with_no_callback) {
    StackFixture f(makePage("main"));
    bool called = false;
    ResultPage page;
    page.name = "dialog";
    page.kind = PageKind::Dialog;
    f.stack.openForResult(page, [&](const int&) { called = true; });
    f.endFrame();
    pageLog.clear();
    f.stack.exit();
    f.endFrame();
    CHECK(isLog({"dialog reset", "dialog exit", "main exit"}));
    CHECK(!called);
}

TEST(close_of_the_bottom_page_ends_the_stack) {
    PlainPage main    = makePage("main");
    main.onShortPress = [](TestPageImpl<PlainPage>& impl, Button) { impl.doClose(); };
    StackFixture f(main);
    f.tap(Button::B);
    CHECK(f.stack.isDone());
    CHECK(!f.stack.takeQuitRequest());
}

namespace {

/** A legacy page whose module calls run */
struct LegacyTestPage final : public UiPage {
    using Result = std::monostate;
    std::function<ModuleExitReason(DisplayContext*)> run;

    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

class LegacyTestPageImpl final : public LegacyModulePageImpl<LegacyTestPage> {
public:
    explicit LegacyTestPageImpl(const LegacyTestPage& page) : page(page) {}
    void onExit() override { pageLog.push_back("legacy exit"); }

protected:
    ModuleExitReason runModule(DisplayContext* display) override { return page.run(display); }

private:
    LegacyTestPage page;
};

std::unique_ptr<UiPageImplBase> LegacyTestPage::createPageImpl() const {
    return std::make_unique<LegacyTestPageImpl>(*this);
}

class TestLegacyHost : public LegacyModuleHost {
public:
    SDL_Surface* screen = nullptr;
    bool quitRequested  = false;

    void beforeModule() override { pageLog.push_back("before module"); }
    void afterModule() override { pageLog.push_back("after module"); }
    SDL_Surface* getScreen() override { return screen; }
    uint32_t getTime() override { return 1000; }
    void requestQuit() override { quitRequested = true; }
};

struct LegacyFixture {
    FakePlaybackControl playback;
    PageStack stack{playback};
    UiCompositor compositor;
    TestLegacyHost host;

    explicit LegacyFixture(const PlainPage& bottom = makePage("main")) {
        pageLog.clear();
        layoutLog.clear();
        host.screen = SDL_CreateRGBSurfaceWithFormat(0, 40, 20, 32, SDL_PIXELFORMAT_ARGB8888);
        compositor.setScreen(host.screen);
        stack.start(bottom.createPageImpl(), compositor);
    }

    uint32_t getScreenPixel() const { return *static_cast<const uint32_t*>(host.screen->pixels); }

    ~LegacyFixture() {
        stack.closeAll();
        SDL_FreeSurface(host.screen);
    }

    /** Lays out and draws one frame, and returns true when it wrote the surface */
    bool drawFrame() {
        stack.layout(compositor);
        compositor.drawFrame();
        return compositor.hasSurfaceChanged();
    }
};

}  // namespace

// The stack lays out the page under a dialog one time, for its snapshot, and again only after a
// new context
TEST(paused_page_is_not_laid_out) {
    LegacyFixture f;
    f.drawFrame();
    f.stack.open(makePage("dialog", PageKind::Dialog));
    f.stack.endFrame(0);
    layoutLog.clear();
    f.drawFrame();
    f.drawFrame();
    CHECK(layoutLog == (std::vector<std::string>{"main", "dialog", "dialog"}));
    layoutLog.clear();
    f.stack.setContext(f.compositor);
    f.drawFrame();
    CHECK(layoutLog == (std::vector<std::string>{"main", "dialog"}));
}

// The status group of the page under a dialog shows its new values at once after the close
TEST(closed_dialog_refreshes_the_status_group) {
    LegacyFixture f;
    f.drawFrame();
    f.stack.open(makePage("dialog", PageKind::Dialog));
    f.stack.endFrame(0);
    f.drawFrame();
    InputState::getInstance().clearStatusChanged();
    static_cast<TestPageImpl<PlainPage>*>(f.stack.getTop())->doClose();
    f.stack.endFrame(0);
    CHECK(InputState::getInstance().isStatusChanged());
}

// The page under a dialog shows as its snapshot with the dim over it, and in full after the close
TEST(covered_page_shows_dimmed_under_a_dialog) {
    PlainPage main = makePage("main");
    main.color     = 0xFFFFFFFF;
    LegacyFixture f(main);
    f.stack.setDimColor(SDL_Color{0, 0, 0, 128});
    f.drawFrame();
    CHECK(f.getScreenPixel() == 0xFFFFFFFF);
    f.stack.open(makePage("dialog", PageKind::Dialog));
    f.stack.endFrame(0);
    f.drawFrame();
    // White with half of the black over it
    CHECK(f.getScreenPixel() == 0xFF7F7F7F);
    static_cast<TestPageImpl<PlainPage>*>(f.stack.getTop())->doClose();
    f.stack.endFrame(0);
    f.drawFrame();
    CHECK(f.getScreenPixel() == 0xFFFFFFFF);
}

// The module makes the display again at the same address: the stack takes the screen again, gives
// each page its context, and draws the whole screen
TEST(legacy_module_takes_the_screen_again_at_the_same_address) {
    LegacyFixture f;
    f.drawFrame();
    CHECK(!f.drawFrame());
    LegacyTestPage page;
    page.run = [](DisplayContext*) { return MODULE_EXIT_TO_MENU; };
    f.stack.open(page);
    f.stack.endFrame(0);
    pageLog.clear();
    CHECK(runLegacyModuleOfStack(f.stack, f.compositor, f.host, nullptr));
    CHECK(isLog({"before module", "after module", "main setContext", "legacy exit",
                 "main resume"}));
    CHECK(f.drawFrame());
    CHECK(!f.drawFrame());
    CHECK(!runLegacyModuleOfStack(f.stack, f.compositor, f.host, nullptr));
}

// The module leaves pixels on the animation layer and does not make the display again: the first
// frame after it shows no pixel of the module on that layer
TEST(legacy_module_leaves_no_pixels_on_the_layers) {
    LegacyFixture f;
    LegacyTestPage page;
    page.run = [](DisplayContext*) {
        UiLayer_blit(nullptr, 0, 0, UI_LAYER_ANIMATION);
        UiLayer_blit(nullptr, 0, 0, UI_LAYER_STATUS);
        return MODULE_EXIT_BACK;
    };
    f.stack.open(page);
    f.stack.endFrame(0);
    runLegacyModuleOfStack(f.stack, f.compositor, f.host, nullptr);
    f.drawFrame();
    CHECK(!layerHasPixels[UI_LAYER_ANIMATION]);
    CHECK(!layerHasPixels[UI_LAYER_STATUS]);
    CHECK(!f.host.quitRequested);
}

// A module that quits with no quit flag, as Settings does at a self-update restart
TEST(legacy_module_that_quits_requests_the_quit) {
    LegacyFixture f;
    LegacyTestPage page;
    page.run = [](DisplayContext*) { return MODULE_EXIT_QUIT; };
    f.stack.open(page);
    f.stack.endFrame(0);
    runLegacyModuleOfStack(f.stack, f.compositor, f.host, nullptr);
    CHECK(f.host.quitRequested);
}

int main(void) {
    printf("test_page_stack\n");
    RUN(new_page_gets_its_context_before_enter);
    RUN(open_puts_the_page_on_the_top_at_the_end_of_the_frame);
    RUN(second_command_of_a_frame_is_ignored);
    RUN(events_after_the_first_command_reach_no_page);
    RUN(events_after_a_command_reach_no_owner);
    RUN(release_of_start_then_x_in_one_poll_opens_only_the_help);
    RUN(dialog_gets_no_event_of_the_opening_gesture);
    RUN(close_of_a_page_with_a_result_gives_the_default);
    RUN(callback_runs_before_the_page_under_resumes);
    RUN(callback_that_calls_exit_ends_the_stack);
    RUN(open_then_exit_in_one_frame_opens_and_does_not_quit);
    RUN(exit_then_open_in_one_frame_quits);
    RUN(dialog_cannot_open_a_page);
    RUN(covered_page_pauses_and_resumes);
    RUN(exit_closes_each_page_from_the_top_with_no_callback);
    RUN(close_of_the_bottom_page_ends_the_stack);
    RUN(paused_page_is_not_laid_out);
    RUN(closed_dialog_refreshes_the_status_group);
    RUN(covered_page_shows_dimmed_under_a_dialog);
    RUN(legacy_module_takes_the_screen_again_at_the_same_address);
    RUN(legacy_module_leaves_no_pixels_on_the_layers);
    RUN(legacy_module_that_quits_requests_the_quit);
    return test_summary();
}
