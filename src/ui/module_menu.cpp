#include "ui/module_menu.h"

#include <array>

#include "background.h"
#include "input/input_state.hpp"
#include "list_nav.h"
#include "module_common.h"
#include "resume.h"
#include "toast.h"
#include "ui_layers.h"
#include "ui_main.h"
#include "ui_page.hpp"
#include "ui_utils.h"

using namespace input;

namespace ui {
namespace {

// How long the exit prompt stays up, which is also how long the confirming press has to
// arrive: the prompt being on screen is the armed state.
constexpr uint32_t kExitConfirmationWindowMs = 3000;

// How long to show the "Exiting..." toast before the app quits. The last frame of the app
// stays on the screen until NextUI draws its first frame, thus the toast must reach the screen.
constexpr uint32_t kExitToastDelayMs = 100;

constexpr std::array<ButtonRequest, 2> kRequests = {
    request::Repeat{Button::DpadUp},
    request::Repeat{Button::DpadDown},
};

// The exit prompt, while it is showing. TOAST_TOKEN_NONE = not armed.
ToastToken exitPrompt = TOAST_TOKEN_NONE;

// The item the cursor was on when the menu was last left. MENU_NONE on the first visit, thus
// the cursor takes row 0: Resume or Now Playing where one exists, and Library otherwise.
MenuSelection lastSelection = MENU_NONE;

// State of the playback row. Now Playing wins over Resume when both apply.
MenuSelection getPlayingItem() {
    if (Background_isPlaying()) return MENU_NOW_PLAYING;
    if (Resume_isAvailable()) return MENU_RESUME;
    return MENU_NONE;
}

ListNavInput toListNavInput(Button button) {
    switch (button) {
        case Button::DpadUp:    return LIST_NAV_UP;
        case Button::DpadDown:  return LIST_NAV_DOWN;
        case Button::DpadLeft:  return LIST_NAV_LEFT;
        case Button::DpadRight: return LIST_NAV_RIGHT;
        default:                return LIST_NAV_NONE;
    }
}

class MainMenuPage final : public UiPage {
public:
    explicit MainMenuPage(SDL_Surface* screen) {
        nav.items_per_page = calc_list_layout(screen).items_per_page;
    }

    std::span<const ButtonRequest> getButtonRequests() const override { return kRequests; }

    HelpId getHelpId() const override { return HELP_MAIN_MENU; }

    void onEnter() override { updateRows(); }

    // Records the input of the frame. onTick() acts on it.
    bool onButtonEvent(const ButtonEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            return record(typedEvent->button);
        }
        if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) {
            return record(typedEvent->button);
        }
        return false;
    }

    void onTick(uint32_t) override {
        // Track advancement and resume saving
        Background_tick();
        if (Background_isPlaying()) ModuleCommon_setAutosleepDisabled(true);
        updateRows();
        act();
        // The software scroll of the Resume row needs a draw in each frame
        if (menu_needs_scroll_redraw()) invalidate();
    }

    void draw(SDL_Surface* screen, bool) override {
        nav.items_per_page = calc_list_layout(screen).items_per_page;
        render_menu(screen, InputState::getInstance().getStatusGroupWidth(), nav.selected,
                    nav.scroll, &rows);
    }

    MenuSelection getSelection() const { return selection; }
    bool isExiting() const { return exiting; }

private:
    // Builds the row map, and puts the cursor on the remembered item. The playback row comes and
    // goes on its own, thus the map can change on a frame with no input.
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

    bool record(Button button) {
        const ListNavInput input = toListNavInput(button);
        if (input != LIST_NAV_NONE) {
            frameInput.nav = static_cast<ListNavInput>(frameInput.nav | input);
            return true;
        }
        switch (button) {
            case Button::A: frameInput.a = true; return true;
            case Button::X: frameInput.x = true; return true;
            case Button::B: frameInput.b = true; return true;
            default:        return false;
        }
    }

    // Does one action for the input of the frame: a move, then A, then X, then B
    void act() {
        const FrameInput input = frameInput;
        frameInput             = {};
        if (input.nav != LIST_NAV_NONE && ListNav_step(&nav, input.nav).moved) {
            lastSelection = MenuRows_selectionAt(&rows, nav.selected);
            UiLayer_clear(UI_LAYER_ANIMATION);
            invalidate();
        } else if (input.a) {
            open();
        } else if (input.x) {
            clearPlayingItem();
        } else if (input.b) {
            askExit();
        }
    }

    void open() {
        const MenuSelection item = MenuRows_selectionAt(&rows, nav.selected);
        // The cursor is off the end
        if (item == MENU_NONE) return;
        UiLayer_clear(UI_LAYER_ANIMATION);
        lastSelection = item;
        selection     = item;
        exitTo(MODULE_EXIT_BACK);
    }

    // Stops the playback or forgets the Resume record, and puts the cursor on the first row
    void clearPlayingItem() {
        const MenuSelection item = MenuRows_selectionAt(&rows, nav.selected);
        if (!MenuRows_isPlayItem(item)) return;
        if (item == MENU_NOW_PLAYING) {
            Background_stopAll();
        } else {
            Resume_clear();
        }
        UiLayer_clear(UI_LAYER_ANIMATION);
        nav.selected  = 0;
        lastSelection = MENU_NONE;
        updateRows();
        invalidate();
    }

    void askExit() {
        if (Toast_isShowing(exitPrompt)) {
            UiLayer_clear(UI_LAYER_ANIMATION);
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
    // Row map as last built. count -1 so the first build always differs.
    MenuRows rows = {
        .playing_item = MENU_NONE,
        .count        = -1,
        .selection    = {},
    };
    // The buttons of the frame, in the order of the actions
    struct FrameInput {
        ListNavInput nav = LIST_NAV_NONE;
        bool a           = false;
        bool x           = false;
        bool b           = false;
    };

    FrameInput frameInput   = {};
    MenuSelection selection = MENU_NONE;
    bool exiting            = false;
};

}  // namespace
}  // namespace ui

MenuSelection MenuModule_run(DisplayContext* display) {
    ui::MainMenuPage page(DisplayHelper_getSurface(display));
    if (runPageLoop(page, display) == MODULE_EXIT_QUIT) {
        // Give the "Exiting..." toast a moment on the screen before NextUI takes over
        if (page.isExiting()) SDL_Delay(ui::kExitToastDelayMs);
        return MENU_QUIT;
    }
    return page.getSelection();
}
