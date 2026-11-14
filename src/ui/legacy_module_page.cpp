#include "ui/legacy_module_page.hpp"

#include <functional>

#include "ui/page_stack.hpp"
#include "ui/view/ui_compositor.hpp"
#include "ui_layers.h"

bool runLegacyModuleOfStack(PageStack& stack, UiCompositor& compositor, LegacyModuleHost& host,
                            DisplayContext* display) {
    const std::function<ModuleExitReason(DisplayContext*)> module = stack.takeLegacyModule();
    if (!module) return false;

    compositor.clearUsedLayers();
    host.beforeModule();
    // A module that quits through a self-update restart does not set the quit flag
    if (module(display) == MODULE_EXIT_QUIT) host.requestQuit();
    UiLayer_clear(UI_LAYER_STATUS);
    UiLayer_clear(UI_LAYER_ANIMATION);
    host.afterModule();

    // A display made again can have the same address, thus the compositor takes the screen in
    // each case, and draws the whole screen
    compositor.setScreen(host.getScreen());
    stack.setContext(compositor);
    stack.completeLegacyModule(host.getTime());
    return true;
}
