#include "ui/main_menu_page.hpp"

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "background.h"
#include "error.h"
#include "resume.h"
#include "selfupdate.h"
#include "toast.h"
#include "ui/library_page.hpp"
#include "ui/now_playing_page.hpp"
#include "ui/page/menu_page.hpp"
#include "ui/podcast_page.hpp"
#include "ui/radio_page.hpp"
#include "ui/resume_page.hpp"
#include "ui/settings_page.hpp"
#include "ui_main.h"
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

/** How often Main reads its rows and their labels again. Both can change with no input. */
constexpr uint32_t kRowRefreshMs = 3000;

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

/** The rows of the main menu. The id of a row is its MenuItem. */
class MainMenuAdapter final : public MenuAdapter {
public:
    /** Called with the item of a row that the user selects */
    std::function<void(MenuItem)> onOpen;

protected:
    std::vector<long> loadRowIds() const override {
        std::vector<long> ids;
        const MenuItem playingItem = getPlayingItem();
        if (playingItem != MenuItem::None) ids.push_back(static_cast<long>(playingItem));
        for (MenuItem item : {MenuItem::Library, MenuItem::Radio, MenuItem::Podcast, MenuItem::Settings}) {
            ids.push_back(static_cast<long>(item));
        }
        return ids;
    }

    MenuRow loadRow(long id) const override {
        const MenuItem item = static_cast<MenuItem>(id);
        return {
            .id       = id,
            .label    = getRowLabel(item),
            .onSelect = [this, item] { onOpen(item); },
        };
    }
};

class MainMenuPageImpl final : public MenuPageImpl<MainMenuPage> {
public:
    MainMenuPageImpl() : MenuPageImpl("Music Player", HELP_MAIN_MENU) {
        setBackText("EXIT");
        items.onOpen = [this](MenuItem item) { openItem(item); };
    }

    void onEnter() override {
        start(items);
        getScheduler().postRepeating([this] { refreshRows(); }, kRowRefreshMs);
        Error_showToast();
    }

    /** A legacy module can change the playback, and with it the rows and their labels */
    void resume() override { refreshRows(); }

    /** While the "Exiting..." toast shows, no button acts */
    bool onRawEvent(const RawEvent&) override { return exiting; }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (exiting) return true;
        if (event.isSingleButton(Button::X)) {
            clearPlayingItem();
            return true;
        }
        return MenuPageImpl::onButtonEvent(event);
    }

private:
    void onBack() override { askExit(); }

    void refreshRows() {
        items.reload();
        Error_showToast();
    }

    /** Returns the item of the row of the cursor, or None for an empty menu. */
    MenuItem getSelectedItem() const {
        const MenuRow* row = getSelectedRow();
        return row ? static_cast<MenuItem>(row->id) : MenuItem::None;
    }

    void openItem(MenuItem item) {
        switch (item) {
            case MenuItem::NowPlaying: openNowPlaying(); break;
            case MenuItem::Resume:     getNavigator().open(ResumePage{}); break;
            case MenuItem::Library:    getNavigator().open(LibraryPage{}); break;
            case MenuItem::Radio:      getNavigator().open(RadioPage{}); break;
            case MenuItem::Podcast:    getNavigator().open(PodcastPage{}); break;
            case MenuItem::Settings:   getNavigator().open(SettingsPage{}); break;
            case MenuItem::None:       break;
        }
    }

    /** The radio opens its station list, which opens the playing screen at once */
    void openNowPlaying() {
        if (Background_getActive() == BG_RADIO) {
            getNavigator().open(RadioPage{});
        } else {
            getNavigator().open(NowPlayingPage{});
        }
    }

    /** Stops the playback or forgets the Resume record of the row of the cursor */
    void clearPlayingItem() {
        const MenuItem selectedItem = getSelectedItem();
        if (selectedItem == MenuItem::NowPlaying) {
            Background_stopAll();
            items.reload();
        } else if (selectedItem == MenuItem::Resume) {
            Resume_clear();
            items.reload();
        }
    }

    void askExit() {
        if (Toast_isShowing(exitPrompt)) {
            // Unbound: it has to outlive the menu as the last frame on the screen
            Toast_show("Exiting...", TOAST_DURATION);
            exiting = true;
            getScheduler().postDelayed([this] { getNavigator().exit(); }, kExitToastDelayMs);
        } else {
            exitPrompt = Toast_showScreenBound("Press B again to exit", kExitConfirmationWindowMs);
        }
    }

    MainMenuAdapter items;
    /** The "Exiting..." toast shows before the quit */
    bool exiting = false;
};

}  // namespace
}  // namespace ui

std::unique_ptr<UiPageImplBase> MainMenuPage::createPageImpl() const {
    return std::make_unique<ui::MainMenuPageImpl>();
}
