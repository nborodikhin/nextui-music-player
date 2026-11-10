#include "ui/module_main_menu.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <string>

#include "background.h"
#include "input/input_state.hpp"
#include "module_common.h"
#include "resume.h"
#include "stopwatch.h"
#include "toast.h"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/ui_page.hpp"
#include "ui_fonts.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_utils.h"

using namespace input;

namespace ui {
namespace {

/**
 * How long the exit prompt stays up, which is also how long the confirming press has to
 * arrive: the prompt being on screen is the armed state.
 */
constexpr uint32_t kExitConfirmationWindowMs = 3000;

/**
 * How long to show the "Exiting..." toast before the app quits. The last frame of the app
 * stays on the screen until NextUI draws its first frame, thus the toast must reach the screen.
 */
constexpr uint32_t kExitToastDelayMs = 100;

/** The count of the items of the menu, each a value of MenuSelection from 0. */
constexpr size_t kMenuItemCount = MENU_SETTINGS + 1;

constexpr const char* kLeftHints[]  = {"START", "CONTROLS", nullptr};
constexpr const char* kRightHints[] = {"B", "EXIT", "A", "OPEN", nullptr};

constexpr std::array<ButtonRequest, 8> kRequests = {
    request::Repeat{Button::DpadUp},
    request::Repeat{Button::DpadDown},
    request::Repeat{Button::DpadLeft},
    request::Repeat{Button::DpadRight},
    request::Repeat{Button::AnalogUp},
    request::Repeat{Button::AnalogDown},
    request::Repeat{Button::AnalogLeft},
    request::Repeat{Button::AnalogRight},
};

/** The exit prompt, while it is showing. TOAST_TOKEN_NONE = not armed. */
ToastToken exitPrompt = TOAST_TOKEN_NONE;

/**
 * The item the cursor was on when the menu was last left. MENU_NONE on the first visit, thus
 * the cursor takes row 0: Resume or Now Playing where one exists, and Library otherwise.
 */
MenuSelection lastSelection = MENU_NONE;

/** State of the playback row. Now Playing wins over Resume when both apply. */
MenuSelection getPlayingItem() {
    if (Background_isPlaying()) return MENU_NOW_PLAYING;
    if (Resume_isAvailable()) return MENU_RESUME;
    return MENU_NONE;
}

struct ScrollTimers {
    Stopwatch startDelay;
    Stopwatch scrolling;
};

/** The rows of the main menu, and the view of each row. */
class MainMenuItems final : public ListAdapter {
public:
    /** Row map as last built. count -1 so the first build always differs. */
    MenuRows rows = {
        .playing_item = MENU_NONE,
        .count        = -1,
        .selection    = {},
    };

    MenuSelection getItem(int index) const { return MenuRows_selectionAt(&rows, index); }

    int getCount() const override { return std::max(0, rows.count); }

    long getItemId(int index) const override { return getItem(index); }

    /** Each item keeps its view while the menu runs, thus the Resume row keeps its scrolling. */
    std::shared_ptr<UiView> getView(int index) override {
        std::shared_ptr<ListRowView>& row = rowViews[static_cast<size_t>(getItem(index))];
        if (!row) row = std::make_shared<ListRowView>(Fonts_getLarge());
        return row;
    }

private:
    std::array<std::shared_ptr<ListRowView>, kMenuItemCount> rowViews;
};

class MainMenuPage final : public UiPage {
public:
    MainMenuPage() {
        screenView.getTitleView().setText("Music Player");
        screenView.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screenView.getButtonHintsView().setHints(kLeftHints, kRightHints);
    }

    std::span<const ButtonRequest> getButtonRequests() const override { return kRequests; }

    HelpId getHelpId() const override { return HELP_MAIN_MENU; }

    void onEnter() override {
        const uint32_t now = SDL_GetTicks();
        restartScrolling(rowTimers, now);
        restartScrolling(titleTimers, now);
        getList().setAdapter(items);
        updateRows();
    }

    void setContext(UiContext& context) override { screenView.setContext(context); }

    void layout(UiLayoutContext& context) override {
        const UiSize screen = context.getMaxSize();
        context.layoutChild(screenView, SDL_Rect{0, 0, screen.w, screen.h});
    }

    void pause(uint32_t now) override {
        rowTimers.scrolling.stop(now);
        titleTimers.scrolling.stop(now);
    }

    void resume(uint32_t now) override {
        resumeScrolling(rowTimers, now);
        resumeScrolling(titleTimers, now);
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            return pressButton(typedEvent->button);
        }
        if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) {
            return pressButton(typedEvent->button);
        }
        return false;
    }

    void onTick(uint32_t now) override {
        // Track advancement and resume saving
        Background_tick();
        updateRows();
        updateLabels(now);
        for (ScrollTimers* timers : {&titleTimers, &rowTimers}) {
            Stopwatch& startDelay = timers->startDelay;
            if (startDelay.isRunning() && startDelay.getElapsed(now) >= SCROLL_START_DELAY) {
                startDelay.stop(now);
                timers->scrolling.start(now);
            }
        }
        screenView.getTitleView().updateScrolling(titleTimers.scrolling.getElapsed(now));
        const int selected = getList().getSelectedIndex();
        if (selected >= 0) {
            getRow(selected).updateScrolling(rowTimers.scrolling.getElapsed(now));
        }
    }

    MenuSelection getSelection() const { return selection; }
    bool isExiting() const { return exiting; }

private:
    /**
     * Builds the row map, and puts the cursor on the remembered item. The playback row comes and
     * goes on its own, thus the map can change on a frame with no input.
     */
    void updateRows() {
        const MenuRows built = MenuRows_build(getPlayingItem());
        const bool changed   = !MenuRows_equal(&built, &items.rows);
        items.rows           = built;
        if (changed) getList().notifyDataChanged();

        const MenuRows& rows = items.rows;
        const int row        = MenuRows_isPlayItem(lastSelection)
                                   ? MenuRows_getPlayingItemRow(&rows)
                                   : MenuRows_rowOf(&rows, lastSelection);
        // A remembered item that is gone leaves the cursor where the list put it
        if (row >= 0) getList().setSelectedIndex(row);
        lastSelection = items.getItem(getList().getSelectedIndex());
        showSelection();
    }

    /** Puts the selection pill on the row of the cursor, and restarts the wait of its text. */
    void showSelection() {
        const int selected = getList().getSelectedIndex();
        for (int index = 0; index < getList().getItemCount(); index++) {
            getRow(index).setSelected(index == selected);
        }
        const MenuSelection item = items.getItem(selected);
        if (item != selectedItem) {
            selectedItem = item;
            // The time of the frame, which the tick of the same frame reads too
            restartScrolling(rowTimers, InputState::getInstance().getTime());
        }
    }

    /** Sets the label of each row. A new label of the selected row restarts the wait of its text. */
    void updateLabels(uint32_t now) {
        for (int index = 0; index < getList().getItemCount(); index++) {
            const MenuSelection item = items.getItem(index);
            char label[MAX_PATH];
            main_menu_get_row_label(item, label, sizeof(label));
            std::string& shown = labels[static_cast<size_t>(item)];
            if (shown == label) continue;
            shown = label;
            getRow(index).setText(label);
            if (index == getList().getSelectedIndex()) restartScrolling(rowTimers, now);
        }
    }

    /** Puts the text at rest, and starts the wait before it scrolls. */
    static void restartScrolling(ScrollTimers& timers, uint32_t now) {
        timers.startDelay.reset();
        timers.startDelay.start(now);
        timers.scrolling.reset();
    }

    /** A dialog stops the scrolling. A wait that a dialog cut starts again from its beginning. */
    static void resumeScrolling(ScrollTimers& timers, uint32_t now) {
        if (timers.startDelay.isRunning()) {
            timers.startDelay.reset();
            timers.startDelay.start(now);
        } else {
            timers.scrolling.start(now);
        }
    }

    ListView& getList() { return screenView.getListView(); }
    ListRowView& getRow(int index) { return static_cast<ListRowView&>(getList().getItemView(index)); }

    /** Returns: true when the button has an action on the menu */
    bool pressButton(Button button) {
        switch (button) {
            case Button::DpadUp:
            case Button::AnalogUp:    showMovedCursor(getList().moveBackward()); return true;
            case Button::DpadDown:
            case Button::AnalogDown:  showMovedCursor(getList().moveForward()); return true;
            case Button::DpadLeft:
            case Button::AnalogLeft:  showMovedCursor(getList().pageBackward()); return true;
            case Button::DpadRight:
            case Button::AnalogRight: showMovedCursor(getList().pageForward()); return true;
            case Button::A: openItem(); return true;
            case Button::X: clearPlayingItem(); return true;
            case Button::B: askExit(); return true;
            default:        return false;
        }
    }

    void showMovedCursor(bool moved) {
        if (!moved) return;
        lastSelection = items.getItem(getList().getSelectedIndex());
        showSelection();
    }

    void openItem() {
        const MenuSelection item = items.getItem(getList().getSelectedIndex());
        // The cursor is off the end
        if (item == MENU_NONE) return;
        lastSelection = item;
        selection     = item;
        exitTo(MODULE_EXIT_BACK);
    }

    /** Stops the playback or forgets the Resume record, and puts the cursor on the first row */
    void clearPlayingItem() {
        const MenuSelection item = items.getItem(getList().getSelectedIndex());
        if (!MenuRows_isPlayItem(item)) return;
        if (item == MENU_NOW_PLAYING) {
            Background_stopAll();
        } else {
            Resume_clear();
        }
        getList().setSelectedIndex(0);
        lastSelection = MENU_NONE;
        updateRows();
    }

    void askExit() {
        if (Toast_isShowing(exitPrompt)) {
            exiting = true;
            // Unbound: it has to outlive the menu as the last frame on the screen
            Toast_show("Exiting...", TOAST_DURATION);
            exitTo(MODULE_EXIT_QUIT);
        } else {
            exitPrompt = Toast_showScreenBound("Press B again to exit", kExitConfirmationWindowMs);
        }
    }

    MainMenuItems items;
    MenuSelection selection = MENU_NONE;
    bool exiting            = false;

    ListScreenView screenView{Fonts_getLarge()};
    std::array<std::string, kMenuItemCount> labels;
    MenuSelection selectedItem  = MENU_NONE;
    ScrollTimers rowTimers;
    ScrollTimers titleTimers;
};

}  // namespace
}  // namespace ui

MenuSelection MenuModule_run(DisplayContext* display) {
    ui::MainMenuPage page;
    if (runPageLoop(page, display) == MODULE_EXIT_QUIT) {
        // Give the "Exiting..." toast a moment on the screen before NextUI takes over
        if (page.isExiting()) SDL_Delay(ui::kExitToastDelayMs);
        return MENU_QUIT;
    }
    return page.getSelection();
}
