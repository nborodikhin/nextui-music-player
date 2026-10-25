#include "ui_page.hpp"

#include <utility>
#include <vector>

#include "input/button_detector.hpp"
#include "input/common_button_handler.hpp"
#include "input/input_driver.hpp"
#include "input/input_manager.hpp"
#include "input/input_state.hpp"
#include "pad_reset.h"
#include "ui_layers.h"
#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

// Gives each raw event to the input manager, and then to the detector. The input manager finds
// the owner of a press before the detector makes the button events of that press.
class RawEventFanOut : public RawEventListener {
public:
    RawEventFanOut(InputManager& manager, ButtonDetector& detector)
        : manager(manager), detector(detector) {}

    bool onRawEvent(const RawEvent& event) override {
        manager.onRawEvent(event);
        detector.onRawEvent(event);
        return false;
    }

private:
    InputManager& manager;
    ButtonDetector& detector;
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

class PlatformPlaybackControl : public PlaybackControl {
public:
    void togglePlayback() override { ModuleCommon_togglePlayback(); }
    void skipTrack(bool next) override { ModuleCommon_skipTrack(next); }
    void changeVolume(int step) override { ModuleCommon_changeVolume(step); }
};

// A dialog that a global button asked for. The page loop runs it after the events of the frame.
enum class Dialog : uint8_t {
    None,
    Quit,
    ControlsHelp,
};

// Runs the dialog in a frame loop of its own. The loop polled the pad, thus the pad is reset:
// the driver and the detector forget each button that changed while the dialog was open.
void runDialog(Dialog dialog, HelpId helpId, DisplayContext* display) {
    switch (dialog) {
        case Dialog::None:         return;
        case Dialog::Quit:         ModuleCommon_runQuitDialog(display); break;
        case Dialog::ControlsHelp: ModuleCommon_runControlsHelp(display, helpId); break;
    }
    PadReset_run();
}

}  // namespace

ModuleExitReason runPageLoop(UiPage& page, DisplayContext* display) {
    PlatformPlaybackControl playback;
    CommonButtonHandler commonButtonHandler(playback);
    // A dialog runs after the events of the frame. A dialog that ran inside a listener would let
    // the remaining events of the frame reach the page after the dialog closed.
    Dialog pendingDialog = Dialog::None;
    // A page with no controls help takes no dialog, and START still does nothing on it, as on a
    // legacy module
    commonButtonHandler.setHelpRequestedCallback([&] {
        if (page.getHelpId() != HELP_NONE) pendingDialog = Dialog::ControlsHelp;
    });
    commonButtonHandler.setQuitRequestedCallback([&] { pendingDialog = Dialog::Quit; });

    InputManager manager({&page, &commonButtonHandler});
    std::vector<ButtonRequest> requests;
    const auto globalRequests = CommonButtonHandler::getButtonRequests();
    requests.assign(globalRequests.begin(), globalRequests.end());
    const auto pageRequests = page.getButtonRequests();
    requests.insert(requests.end(), pageRequests.begin(), pageRequests.end());
    ButtonDetector detector(manager, requests);
    RawEventFanOut rawInput(manager, detector);
    InputDriver driver;
    StatusGroup statusGroup;
    InputState& state = InputState::getInstance();

    ScreenTitle_start(false);
    SDL_Surface* lastScreen = DisplayHelper_getSurface(display);
    page.onScreenChanged(lastScreen);
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
        SDL_Surface* screen = DisplayHelper_getSurface(display);
        if (ModuleCommon_isQuitRequested()) {
            page.onExit();
            return MODULE_EXIT_QUIT;
        }

        // frameBegin() opens the lock at SELECT + A, thus the frame reads it after that
        state.setKeyLocked(ModuleCommon_isKeyLocked());
        driver.poll(rawInput);
        const uint32_t now = state.getTime();
        detector.onTick(now);

        if (pendingDialog != Dialog::None) {
            runDialog(pendingDialog, page.getHelpId(), display);
            pendingDialog = Dialog::None;
            if (ModuleCommon_isQuitRequested()) {
                page.onExit();
                return MODULE_EXIT_QUIT;
            }
            // The dialog drew over the screen, and a new output can have come in the meantime
            screen = DisplayHelper_getSurface(display);
            page.invalidateAll();
            clearScreen   = true;
            statusChanged = true;
        }

        statusChanged = commonButtonHandler.onTick(now) || statusChanged;
        // A new output made the display again: the screen holds nothing
        const bool outputSwitched = ModuleCommon_takeOutputSwitched();
        statusChanged             = statusChanged || outputSwitched;
        // A new surface and the sleep leave the screen empty. A new surface can have a new size.
        if (outputSwitched || commonButtonHandler.hasWoken() || screen != lastScreen) {
            lastScreen = screen;
            page.onScreenChanged(screen);
            page.invalidateAll();
            clearScreen = true;
        }
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
        // A frame of the title draws on the screen outside of the page
        if (titleDirty) {
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
            // The next screen does not know what the page left on the layers
            UiLayer_clear(UI_LAYER_STATUS);
            UiLayer_clear(UI_LAYER_ANIMATION);
            return reason;
        }
    }
}
