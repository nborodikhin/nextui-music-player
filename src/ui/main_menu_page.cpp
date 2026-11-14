#include "ui/main_menu_page.hpp"

#include <array>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "background.h"
#include "resume.h"
#include "selfupdate.h"
#include "stopwatch.h"
#include "toast.h"
#include "ui/library_page.hpp"
#include "ui/now_playing_page.hpp"
#include "ui/podcast_page.hpp"
#include "ui/radio_page.hpp"
#include "ui/resume_page.hpp"
#include "ui/settings_page.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/list_screen_view.hpp"
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
 * How long the "Exiting..." toast shows before the app quits. The last frame of the app stays on
 * the screen until NextUI draws its first frame, thus the toast must reach the screen.
 */
constexpr uint32_t kExitToastDelayMs = 100;

/** How often Main reads the labels of its rows again. A label can change with no input. */
constexpr uint32_t kLabelRefreshMs = 5000;

/** An item of the main menu. */
enum class MenuItem : int8_t {
    None = -1,
    NowPlaying,
    Resume,
    Library,
    Radio,
    Podcast,
    Settings,
};

/** The count of the items of the menu, each a value of MenuItem from 0. */
constexpr size_t kMenuItemCount = static_cast<size_t>(MenuItem::Settings) + 1;

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

/** State of the playback row. Now Playing wins over Resume when both apply. */
MenuItem getPlayingItem() {
    if (Background_isPlaying()) return MenuItem::NowPlaying;
    if (Resume_isAvailable()) return MenuItem::Resume;
    return MenuItem::None;
}

/** Returns the source of the playback that the Now Playing row names. */
const char* getNowPlayingSource() {
    switch (Background_getActive()) {
        case BG_MUSIC:   return "Music";
        case BG_RADIO:   return "Radio";
        case BG_PODCAST: return "Podcast";
        default:         return "Audio";
    }
}

/** Returns the label of the row of item. */
std::string getRowLabel(MenuItem item) {
    switch (item) {
        case MenuItem::NowPlaying: return std::string("Now Playing: ") + getNowPlayingSource();
        case MenuItem::Resume:
            if (const char* label = Resume_getLabel()) return label;
            return "Resume";
        case MenuItem::Library: return "Library";
        case MenuItem::Radio:   return "Online Radio";
        case MenuItem::Podcast: return "Podcasts";
        case MenuItem::Settings:
            return SelfUpdate_getStatus().update_available ? "Settings (Update available)" : "Settings";
        case MenuItem::None:    return "";
    }
    return "";
}

struct ScrollTimers {
    Stopwatch startDelay;
    Stopwatch scrolling;
};

/** The rows of the main menu, and the view of each row. */
class MainMenuListAdapter final : public ListAdapter {
public:
    /**
     * Sets the items of the rows, in order.
     * Returns: true when the rows changed
     */
    bool setRows(const std::vector<MenuItem> &newRows) {
        if (newRows == rows) return false;
        rows = newRows;
        return true;
    }

    /** Returns the item of the row at index, or None for a row that is not there. */
    MenuItem getItem(int index) const {
        if (index < 0 || index >= getCount()) return MenuItem::None;
        return rows[static_cast<size_t>(index)];
    }

    int getCount() const override { return static_cast<int>(rows.size()); }

    long getItemId(int index) const override { return static_cast<long>(getItem(index)); }

    /** Each item keeps its view while the menu runs, thus the Resume row keeps its scrolling. */
    std::shared_ptr<UiView> getView(int index) override {
        std::shared_ptr<ListRowView>& row = rowViews[static_cast<size_t>(getItem(index))];
        if (!row) row = std::make_shared<ListRowView>(Fonts_getLarge());
        return row;
    }

private:
    std::vector<MenuItem> rows;
    std::array<std::shared_ptr<ListRowView>, kMenuItemCount> rowViews;
};

class MainMenuPageImpl final : public UiPageImpl<MainMenuPage> {
public:
    MainMenuPageImpl() : UiPageImpl(PageKind::FullScreen) {
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
        updateRows(now);
    }

    void setContext(UiContext& context) override { screenView.setContext(context); }

    void layout(UiLayoutContext& context) override { context.layoutChild(screenView, context.getMaxRect()); }

    void pause(uint32_t now) override {
        rowTimers.scrolling.stop(now);
        titleTimers.scrolling.stop(now);
    }

    void resume(uint32_t now) override {
        resumeScrolling(rowTimers, now);
        resumeScrolling(titleTimers, now);
        // A legacy module can change the playback, and with it the rows and their labels
        updateRowMap(now);
        updateLabels(now);
    }

    /** While the "Exiting..." toast shows, no button acts */
    bool onRawEvent(const RawEvent&) override { return exitTimer.isRunning(); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (exitTimer.isRunning()) return true;
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            return pressButton(typedEvent->button, event.time);
        }
        if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) {
            return pressButton(typedEvent->button, event.time);
        }
        return false;
    }

    void onTick(uint32_t now) override {
        if (exitTimer.isRunning() && exitTimer.getElapsed(now) >= kExitToastDelayMs) {
            exitTimer.stop(now);
            getNavigator().exit();
            return;
        }
        updateRows(now);
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

private:
    /**
     * Builds the rows, and reads their labels again when the rows change or kLabelRefreshMs
     * passed. The playback row comes and goes on its own, thus the rows can change on a frame
     * with no input.
     */
    void updateRows(uint32_t now) {
        updateRowMap(now);
        if (now - labelsTime >= kLabelRefreshMs) updateLabels(now);
    }

    /** Builds the rows, and sets the labels of new rows. */
    void updateRowMap(uint32_t now) {
        std::vector<MenuItem> rows = {MenuItem::Library, MenuItem::Radio, MenuItem::Podcast, MenuItem::Settings};
        const MenuItem playingItem = getPlayingItem();
        if (playingItem != MenuItem::None) rows.insert(rows.begin(), playingItem);

        if (items.setRows(rows)) {
            getList().notifyDataChanged();
            updateLabels(now);
        }
        showSelection(now);
    }

    /** Puts the selection pill on the row of the cursor, and restarts the wait of its text. */
    void showSelection(uint32_t now) {
        const int selectedIndex = getList().getSelectedIndex();
        for (int index = 0; index < getList().getItemCount(); index++) {
            getRow(index).setSelected(index == selectedIndex);
        }
        const MenuItem newSelectedItem = items.getItem(selectedIndex);
        if (newSelectedItem != selectedItem) {
            selectedItem = newSelectedItem;
            restartScrolling(rowTimers, now);
        }
    }

    /** Sets the label of each row. A new label of the selected row restarts the wait of its text. */
    void updateLabels(uint32_t now) {
        labelsTime              = now;
        const int selectedIndex = getList().getSelectedIndex();
        for (int index = 0; index < getList().getItemCount(); index++) {
            const bool changed = getRow(index).setText(getRowLabel(items.getItem(index)));
            if (changed && index == selectedIndex) restartScrolling(rowTimers, now);
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
    bool pressButton(Button button, uint32_t time) {
        switch (button) {
            case Button::DpadUp:
            case Button::AnalogUp:    showMovedCursor(getList().moveBackward(), time); return true;
            case Button::DpadDown:
            case Button::AnalogDown:  showMovedCursor(getList().moveForward(), time); return true;
            case Button::DpadLeft:
            case Button::AnalogLeft:  showMovedCursor(getList().pageBackward(), time); return true;
            case Button::DpadRight:
            case Button::AnalogRight: showMovedCursor(getList().pageForward(), time); return true;
            case Button::A: openItem(); return true;
            case Button::X: clearPlayingItem(time); return true;
            case Button::B: askExit(time); return true;
            default:        return false;
        }
    }

    void showMovedCursor(bool moved, uint32_t time) {
        if (!moved) return;
        showSelection(time);
    }

    void openItem() {
        switch (selectedItem) {
            case MenuItem::NowPlaying: getNavigator().open(NowPlayingPage{}); break;
            case MenuItem::Resume:     getNavigator().open(ResumePage{}); break;
            case MenuItem::Library:    getNavigator().open(LibraryPage{}); break;
            case MenuItem::Radio:      getNavigator().open(RadioPage{}); break;
            case MenuItem::Podcast:    getNavigator().open(PodcastPage{}); break;
            case MenuItem::Settings:   getNavigator().open(SettingsPage{}); break;
            case MenuItem::None:       break;
        }
    }

    /** Stops the playback or forgets the Resume record of the row of the cursor */
    void clearPlayingItem(uint32_t time) {
        if (selectedItem == MenuItem::NowPlaying) {
            Background_stopAll();
            updateRowMap(time);
        } else if (selectedItem == MenuItem::Resume) {
            Resume_clear();
            updateRowMap(time);
        }
    }

    void askExit(uint32_t time) {
        if (Toast_isShowing(exitPrompt)) {
            // Unbound: it has to outlive the menu as the last frame on the screen
            Toast_show("Exiting...", TOAST_DURATION);
            exitTimer.start(time);
        } else {
            exitPrompt = Toast_showScreenBound("Press B again to exit", kExitConfirmationWindowMs);
        }
    }

    MainMenuListAdapter items;
    /** Runs while the "Exiting..." toast shows before the quit */
    Stopwatch exitTimer;

    ListScreenView screenView{Fonts_getLarge()};
    /** The item of the row of the cursor */
    MenuItem selectedItem = MenuItem::None;
    /** The time of the last read of the labels */
    uint32_t labelsTime = 0;
    ScrollTimers rowTimers;
    ScrollTimers titleTimers;
};

}  // namespace
}  // namespace ui

std::unique_ptr<UiPageImplBase> MainMenuPage::createPageImpl() const {
    return std::make_unique<ui::MainMenuPageImpl>();
}
