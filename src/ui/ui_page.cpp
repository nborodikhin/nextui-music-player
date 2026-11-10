#include "ui/ui_page.hpp"

#include <vector>

#include "input/button_detector.hpp"
#include "input/common_button_handler.hpp"
#include "input/input_driver.hpp"
#include "input/input_manager.hpp"
#include "input/input_state.hpp"
#include "pad_reset.h"
#include "ui/view/ui_compositor.hpp"
#include "ui_theme.h"

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

// The opacity of the page background over a screen that a dialog covers
constexpr uint32_t kDimAlpha = 160;

// Returns the dim of a covered screen, as an ARGB value with straight alpha
uint32_t getDimColor() {
    const SDL_Color color = Theme_getColor(THEME_ROLE_PAGE_BACKGROUND, false);
    return kDimAlpha << 24 | uint32_t{color.r} << 16 | uint32_t{color.g} << 8 | color.b;
}

// Lays out the page in the whole screen, and draws what changed
void composeFrame(UiPage& page, UiCompositor& compositor) {
    page.layout(compositor.beginLayout());
    compositor.drawFrame();
    // The layers record their changes themselves
    if (compositor.hasSurfaceChanged()) ModuleCommon_markSurfaceDrawn();
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
    InputState& state = InputState::getInstance();
    UiCompositor compositor;

    page.onEnter();
    SDL_Surface* screen     = nullptr;
    SDL_Surface* lastScreen = nullptr;

    while (true) {
        // The present comes at the same point after the wait in each frame, and the work of
        // the frame goes after it. The first frame has nothing to present. The frame clock
        // starts before the present, thus the frame budget includes the present.
        if (lastScreen) GFX_sync();
        GFX_startFrame();
        if (lastScreen) ModuleCommon_present(lastScreen);
        ModuleCommon_frameBegin(false);
        screen = DisplayHelper_getSurface(display);
        if (ModuleCommon_isQuitRequested()) {
            page.onExit();
            return MODULE_EXIT_QUIT;
        }

        // A new output made the display again, and a new surface can have a new size
        const bool outputSwitched = ModuleCommon_takeOutputSwitched();
        if (screen != lastScreen || outputSwitched) {
            compositor.setScreen(screen);
            page.setContext(compositor);
            state.setStatusChanged();
        }
        lastScreen = screen;

        // frameBegin() opens the lock at SELECT + A, thus the frame reads it after that
        state.setKeyLocked(ModuleCommon_isKeyLocked());
        driver.poll(rawInput);
        uint32_t now = state.getTime();
        detector.onTick(now);

        if (pendingDialog != Dialog::None) {
            // The page shows dimmed on the regular surface only under the dialog, with each
            // scrolling text stopped where it is
            page.pause(now);
            compositor.setSurfaceOnly(true);
            composeFrame(page, compositor);
            compositor.dimSurface(getDimColor());
            runDialog(pendingDialog, page.getHelpId(), display);
            pendingDialog = Dialog::None;
            if (ModuleCommon_isQuitRequested()) {
                page.onExit();
                return MODULE_EXIT_QUIT;
            }
            // The clocks continue from the time after the dialog, and the frame reads its time
            // after that. An earlier time would put the count of a clock below 0.
            now = SDL_GetTicks();
            page.resume(now);
            // The dialog drew over the screen, and a new output can have come in the meantime
            compositor.setSurfaceOnly(false);
            screen = DisplayHelper_getSurface(display);
            if (screen != lastScreen) {
                compositor.setScreen(screen);
                page.setContext(compositor);
                lastScreen = screen;
            }
            state.setStatusChanged();
        }

        // A sleep runs inside the tick of the handler
        if (commonButtonHandler.onTick(now)) state.setStatusChanged();
        // The sleep leaves the screen empty. The clocks of the page do not count the sleep, and
        // the frame reads its time after it.
        if (commonButtonHandler.hasWoken()) {
            page.pause(now);
            now = SDL_GetTicks();
            page.resume(now);
            compositor.invalidateAll();
            state.setStatusChanged();
        }
        page.onTick(now);
        composeFrame(page, compositor);

        const ModuleExitReason reason = page.getExitReason();
        if (reason != MODULE_EXIT_NONE) {
            // The last frame reaches the screen, such as a toast of the exit
            ModuleCommon_present(screen);
            page.onExit();
            // The next screen does not know what the page left on the layers
            compositor.clearUsedLayers();
            return reason;
        }
    }
}
