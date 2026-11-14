#ifndef __UI_LEGACY_MODULE_PAGE_HPP__
#define __UI_LEGACY_MODULE_PAGE_HPP__

#include <SDL2/SDL.h>

#include "ui/ui_page.hpp"

class PageStack;
class UiCompositor;

/**
 * The implementation of a page that runs a legacy module in the frame loop of that module. The
 * stack runs the module at the end of the frame where the page opened, and closes the page when
 * the module returns.
 */
template <class Page>
class LegacyModulePageImpl : public UiPageImpl<Page> {
public:
    LegacyModulePageImpl() : UiPageImpl<Page>(PageKind::FullScreen) {}

    void onEnter() override {
        this->getNavigator().runLegacyModule(
            [this](DisplayContext* display) { return runModule(display); });
    }

    /** A legacy page has no views: the module draws the screen. */
    void layout(UiLayoutContext&) override {}

protected:
    /** Runs the module until it exits, and returns its exit reason. */
    virtual ModuleExitReason runModule(DisplayContext* display) = 0;
};

/** The work around a legacy module that depends on the platform. */
class LegacyModuleHost {
public:
    /** Called before the module runs. */
    virtual void beforeModule() = 0;
    /** Called after the module returns, before the stack takes the screen again. */
    virtual void afterModule() = 0;
    virtual SDL_Surface* getScreen() = 0;
    virtual uint32_t getTime() = 0;
    virtual void requestQuit() = 0;

protected:
    ~LegacyModuleHost() = default;
};

/**
 * Runs the legacy module that the top page of stack asked for, and returns false when it asked
 * for none. The module draws the screen and writes the status layer and the animation layer with
 * no record in the compositor. Thus after the module, the stack clears these layers, takes the
 * screen again, and draws the whole screen. Then it closes the legacy page.
 */
bool runLegacyModuleOfStack(PageStack& stack, UiCompositor& compositor, LegacyModuleHost& host,
                            DisplayContext* display);

#endif
