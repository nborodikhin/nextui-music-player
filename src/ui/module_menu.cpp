#include "ui/module_menu.h"

#include <array>

#include "background.h"
#include "input/input_state.hpp"
#include "list_nav.h"
#include "module_common.h"
#include "resume.h"
#include "toast.h"
#include "ui_main.h"
#include "ui_page.hpp"
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

constexpr std::array<ButtonRequest, 4> kRequests = {
    request::Repeat{Button::DpadUp},
    request::Repeat{Button::DpadDown},
    request::Repeat{Button::AnalogUp},
    request::Repeat{Button::AnalogDown},
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

ListNavInput toListNavInput(Button button) {
    switch (button) {
        case Button::DpadUp:
        case Button::AnalogUp:    return LIST_NAV_UP;
        case Button::DpadDown:
        case Button::AnalogDown:  return LIST_NAV_DOWN;
        case Button::DpadLeft:
        case Button::AnalogLeft:  return LIST_NAV_LEFT;
        case Button::DpadRight:
        case Button::AnalogRight: return LIST_NAV_RIGHT;
        default:                  return LIST_NAV_NONE;
    }
}

class MainMenuPage final : public UiPage {
public:
    std::span<const ButtonRequest> getButtonRequests() const override { return kRequests; }

    HelpId getHelpId() const override { return HELP_MAIN_MENU; }

    void onScreenChanged(SDL_Surface* screen) override {
        nav.items_per_page = calc_list_layout(screen).items_per_page;
        // The rows are not built before onEnter()
        if (rows.count >= 0) ListNav_reconcile(&nav, rows.count);
    }

    void onEnter() override { updateRows(); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            return pressButton(typedEvent->button);
        }
        if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) {
            return pressButton(typedEvent->button);
        }
        return false;
    }

    void onTick(uint32_t) override {
        // Track advancement and resume saving
        Background_tick();
        updateRows();
        // The software scroll of the Resume row needs a draw in each frame
        if (menu_needs_scroll_redraw()) invalidate();
    }

    void draw(SDL_Surface* screen, bool) override {
        render_menu(screen, InputState::getInstance().getStatusGroupWidth(), nav.selected,
                    nav.scroll, &rows);
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
        bool changed         = !MenuRows_equal(&built, &rows);
        rows                 = built;

        const int row = MenuRows_isPlayItem(lastSelection) ? MenuRows_getPlayingItemRow(&rows)
                                                           : MenuRows_rowOf(&rows, lastSelection);
        if (row >= 0 && row != nav.selected) {
            nav.selected = row;
            changed      = true;
        }
        // A remembered item that is gone leaves the cursor where it was, which reconcile then
        // clamps into the new map
        if (ListNav_reconcile(&nav, rows.count).moved) changed = true;
        lastSelection = MenuRows_selectionAt(&rows, nav.selected);
        if (changed) invalidate();
    }

    /** Returns: true when the button has an action on the menu */
    bool pressButton(Button button) {
        const ListNavInput navInput = toListNavInput(button);
        if (navInput != LIST_NAV_NONE) {
            moveCursor(navInput);
            return true;
        }
        switch (button) {
            case Button::A: openItem(); return true;
            case Button::X: clearPlayingItem(); return true;
            case Button::B: askExit(); return true;
            default:        return false;
        }
    }

    void moveCursor(ListNavInput navInput) {
        if (!ListNav_step(&nav, navInput).moved) return;
        lastSelection = MenuRows_selectionAt(&rows, nav.selected);
        invalidate();
    }

    void openItem() {
        const MenuSelection item = MenuRows_selectionAt(&rows, nav.selected);
        // The cursor is off the end
        if (item == MENU_NONE) return;
        lastSelection = item;
        selection     = item;
        exitTo(MODULE_EXIT_BACK);
    }

    /** Stops the playback or forgets the Resume record, and puts the cursor on the first row */
    void clearPlayingItem() {
        const MenuSelection item = MenuRows_selectionAt(&rows, nav.selected);
        if (!MenuRows_isPlayItem(item)) return;
        if (item == MENU_NOW_PLAYING) {
            Background_stopAll();
        } else {
            Resume_clear();
        }
        nav.selected  = 0;
        lastSelection = MENU_NONE;
        updateRows();
        invalidate();
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
        invalidate();
    }

    ListNav nav = {
        .selected        = 0,
        .scroll          = 0,
        .count           = 0,
        .items_per_page  = 1,
        .external_scroll = false,
    };
    /** Row map as last built. count -1 so the first build always differs. */
    MenuRows rows = {
        .playing_item = MENU_NONE,
        .count        = -1,
        .selection    = {},
    };
    MenuSelection selection = MENU_NONE;
    bool exiting            = false;
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
