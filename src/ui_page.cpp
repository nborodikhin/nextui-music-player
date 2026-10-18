#include "ui_page.hpp"

#include <initializer_list>
#include <vector>

#include "input/button_detector.hpp"
#include "input/common_button_handler.hpp"
#include "input/input_driver.hpp"
#include "input/input_state.hpp"
#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

// Gives a button event to the common handler, and to the page when the common handler did
// not handle it.
class FirstHandlerButtonListener : public ButtonListener {
public:
    FirstHandlerButtonListener(std::initializer_list<ButtonListener*> chain)
        : listeners(chain) {}

    bool onButtonEvent(const ButtonEvent& event) override {
        for (const auto listener : listeners) {
            if (listener->onButtonEvent(event)) return true;
        }
        return false;
    }

private:
    std::vector<ButtonListener*> listeners;
};

// Gives a raw event to the page, and then to the detector.
class CompositeRawEventListener : public RawEventListener {
public:
    CompositeRawEventListener(std::initializer_list<RawEventListener*> chain)
        : listeners(chain) {}

    void onRawEvent(const RawEvent& event) override {
        for (RawEventListener* listener : listeners) {
            listener->onRawEvent(event);
        }
    }

private:
    std::vector<RawEventListener*> listeners;
};

// Draws the status group, or the hardware bar in its place, in the top right corner of the
// screen. The page draws its title in the room that is left, thus the width is necessary
// before the page draws, and the group draws after the page, above it.
class StatusGroup {
public:
    ~StatusGroup() {
        if (scratch) SDL_FreeSurface(scratch);
    }

    // Returns the width of the group. Draws it on a surface that is not shown, because only
    // a draw gives the width. Returns 0 on a screen with no room for the group.
    int measure(SDL_Surface* screen, HardwareBar bar) {
        if (!screen_has_status_group(screen)) return 0;
        if (!scratch || scratch->w != screen->w || scratch->h != screen->h) {
            if (scratch) SDL_FreeSurface(scratch);
            scratch = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32,
                                                     screen->format->format);
            if (!scratch) return 0;
        }
        return GFX_blitHardwareGroup(scratch, static_cast<int>(bar));
    }

    void draw(SDL_Surface* screen, HardwareBar bar) {
        if (!screen_has_status_group(screen)) return;
        GFX_blitHardwareGroup(screen, static_cast<int>(bar));
    }

private:
    SDL_Surface* scratch = nullptr;
};

}  // namespace

ModuleExitReason runPageLoop(UiPage& page, DisplayContext* display) {
    CommonButtonHandler commonButtonHandler;
    FirstHandlerButtonListener buttonChain({&commonButtonHandler, &page});
    ButtonDetector detector(buttonChain, page.getButtonRequests());
    CompositeRawEventListener rawChain({&page, &detector});
    InputDriver driver;
    StatusGroup statusGroup;
    InputState& state = InputState::getInstance();

    ScreenTitle_start(false);
    page.onEnter();
    page.invalidateAll();
    // The coordinator clears the screen only for its own invalidateAll(). A page that paints
    // only what changed keeps the rest of its last draw.
    bool clearScreen = true;

    // The title asks for the next frame while its marquee moves
    int titleDirty = 0;
    // The status group changes only when the power management asks for a draw
    bool statusChanged = true;
    HardwareBar lastBar = state.getHardwareBar();

    while (true) {
        ModuleCommon_frameBegin();
        SDL_Surface* const screen = DisplayHelper_getSurface(display);
        if (ModuleCommon_stopSignalled()) {
            page.onExit();
            return MODULE_EXIT_QUIT;
        }

        driver.poll(rawChain);
        const uint32_t now = state.getTime();
        detector.onTick(now);
        statusChanged = commonButtonHandler.onTick(now) || statusChanged;
        // A new output made the display again: the screen holds nothing
        const bool outputSwitched = ModuleCommon_takeOutputSwitched();
        statusChanged             = statusChanged || outputSwitched;
        page.onTick(now);

        const HardwareBar bar = state.getHardwareBar();
        // A new width moves the title. A new bar adds or removes the hints at the bottom.
        if (statusChanged) {
            const int width = statusGroup.measure(screen, bar);
            if (width != state.getStatusGroupWidth() || bar != lastBar) {
                state.setStatusGroupWidth(width);
                lastBar = bar;
                page.invalidateAll();
                clearScreen = true;
            }
        }
        // The sleep and a new output cleared the screen, and a frame of the title draws on the
        // screen outside of the page
        if (commonButtonHandler.hasWoken() || outputSwitched || titleDirty) {
            page.invalidateAll();
            clearScreen = true;
        }

        const bool pageDirty = page.isDirty();
        if (pageDirty) {
            if (clearScreen) {
                GFX_clear(screen);
                clearScreen = false;
            }
            // The page is the only entry, thus it owns the layers
            page.draw(screen, true);
            page.markClean();
        }
        if (pageDirty || statusChanged) {
            statusGroup.draw(screen, bar);
            if (bar != HardwareBar::None) {
                GFX_blitHardwareHints(screen, static_cast<int>(bar));
            }
            ModuleCommon_markSurfaceDrawn();
        }
        statusChanged = false;
        titleDirty = 0;
        ScreenTitle_frameEnd(&titleDirty, true);
        ModuleCommon_frameEnd(screen);

        const ModuleExitReason reason = page.getExitReason();
        if (reason != MODULE_EXIT_NONE) {
            page.onExit();
            return reason;
        }
    }
}
