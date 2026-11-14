#include <functional>

#include "background.h"
#include "input/input_driver.hpp"
#include "input/input_state.hpp"
#include "module_common.h"
#include "pad_reset.h"
#include "toast.h"
#include "ui/controls_help_page.hpp"
#include "ui/legacy_module_page.hpp"
#include "ui/page_stack.hpp"
#include "ui/quit_dialog_page.hpp"
#include "ui/view/ui_compositor.hpp"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

/** How much of the page under a dialog shows through the dim, of 255. */
constexpr uint8_t kDimAlpha = 160;

class PlatformPlaybackControl : public PlaybackControl {
public:
    void togglePlayback() override { ModuleCommon_togglePlayback(); }
    void skipTrack(bool next) override { ModuleCommon_skipTrack(next); }
    void changeVolume(int step) override { ModuleCommon_changeVolume(step); }
};

SDL_Color getDimColor() {
    SDL_Color color = Theme_getColor(THEME_ROLE_PAGE_BACKGROUND, false);
    color.a         = kDimAlpha;
    return color;
}

class PlatformLegacyModuleHost : public LegacyModuleHost {
public:
    explicit PlatformLegacyModuleHost(DisplayContext* display) : display(display) {}

    void beforeModule() override { Toast_screenChanged(); }

    void afterModule() override {
        Toast_screenChanged();
        // The module polled the pad in its own loop
        PadReset_run();
        InputState::getInstance().setStatusChanged();
    }

    SDL_Surface* getScreen() override { return DisplayHelper_getSurface(display); }
    uint32_t getTime() override { return SDL_GetTicks(); }
    void requestQuit() override { ModuleCommon_requestQuit(); }

private:
    DisplayContext* display;
};

void runFrameLoop(PageStack& stack, DisplayContext* display, UiCompositor& compositor) {
    InputDriver driver;
    InputState& state = InputState::getInstance();
    PlatformLegacyModuleHost legacyHost(display);
    SDL_Surface* screen = DisplayHelper_getSurface(display);
    // The first frame and the frame after a legacy module have nothing to present
    bool hasFrame = false;

    while (true) {
        // The present comes at the same point after the wait in each frame, and the work of
        // the frame goes after it. The frame clock starts before the present, thus the frame
        // budget includes the present.
        if (hasFrame) GFX_sync();
        GFX_startFrame();
        if (hasFrame) ModuleCommon_present(screen);
        ModuleCommon_frameBegin(false);
        if (ModuleCommon_isQuitRequested()) {
            stack.closeAll();
            compositor.clearUsedLayers();
            return;
        }

        // A new output made the display again, and a new surface can have a new size
        SDL_Surface* newScreen    = DisplayHelper_getSurface(display);
        const bool outputSwitched = ModuleCommon_takeOutputSwitched();
        if (newScreen != screen || outputSwitched) {
            screen = newScreen;
            compositor.setScreen(screen);
            stack.setContext(compositor);
            state.setStatusChanged();
        }

        // frameBegin() opens the lock at SELECT + A, thus the frame reads it after that
        state.setKeyLocked(ModuleCommon_isKeyLocked());
        stack.beginPoll();
        driver.poll(stack.getRawInput());
        uint32_t now = state.getTime();
        stack.tickInput(now);

        // A sleep runs inside the tick of the handler
        CommonButtonHandler& handler = stack.getCommonButtonHandler();
        if (handler.onTick(now)) state.setStatusChanged();
        // The sleep leaves the screen empty. The clocks of the page do not count the sleep, and
        // the frame reads its time after it.
        if (handler.hasWoken()) {
            stack.getTop()->pause(now);
            now = SDL_GetTicks();
            stack.getTop()->resume(now);
            compositor.invalidateAll();
            state.setStatusChanged();
        }
        Background_tick();
        stack.tickTop(now);

        stack.layout(compositor);
        compositor.drawFrame();
        // The layers record their changes themselves
        if (compositor.hasSurfaceChanged()) ModuleCommon_markSurfaceDrawn();
        hasFrame = true;

        stack.endFrame(now);
        if (stack.takeQuitRequest()) ModuleCommon_requestQuit();

        if (runLegacyModuleOfStack(stack, compositor, legacyHost, display)) {
            screen   = DisplayHelper_getSurface(display);
            hasFrame = false;
        }

        if (stack.isDone()) {
            // The last frame reaches the screen, such as a toast of the exit
            if (hasFrame) ModuleCommon_present(screen);
            // The next screen does not know what the stack left on the layers
            compositor.clearUsedLayers();
            return;
        }
    }
}

}  // namespace

void PageStack::run(const UiPage& page, DisplayContext* display) {
    if (ModuleCommon_stopSignalled()) return;
    PlatformPlaybackControl playback;
    PageStack stack(playback);
    stack.setDimColor(getDimColor());
    stack.setDialogOpeners([&stack](HelpId helpId) { stack.open(ControlsHelpPage(helpId)); },
                           [&stack] {
                               stack.openForResult(QuitDialogPage{}, [&stack](bool quit) {
                                   if (quit) stack.exit();
                               });
                           });
    UiCompositor compositor;
    compositor.setScreen(DisplayHelper_getSurface(display));
    stack.start(page.createPageImpl(), compositor);
    runFrameLoop(stack, display, compositor);
}
