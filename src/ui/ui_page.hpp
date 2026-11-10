#ifndef __UI_UI_PAGE_HPP__
#define __UI_UI_PAGE_HPP__

#include <cstdint>
#include <span>

#include "display_helper.h"
#include "help_screen.h"
#include "input/input_listeners.hpp"
#include "module_common.h"
#include "ui/view/ui_context.hpp"
#include "ui/view/ui_layout_context.hpp"

/**
 * A screen that runPageLoop() operates. It gets the raw events and the button events of each
 * frame, and a tick. Its views lay themselves out on each frame, and the compositor of the
 * loop draws what changed.
 */
class UiPage : public input::InputListener {
public:
    /** Returns the long presses, the repeats and the combos that the page uses. */
    virtual std::span<const input::ButtonRequest> getButtonRequests() const = 0;

    virtual HelpId getHelpId() const { return HELP_NONE; }

    /** Called one time before the first frame, and one time after the last frame. */
    virtual void onEnter() {}
    virtual void onExit() {}

    bool onRawEvent(const input::RawEvent&) override { return false; }
    bool onButtonEvent(const input::ButtonEvent&) override { return false; }

    /** Called each frame after the events, with the time of the frame. */
    virtual void onTick(uint32_t) {}

    /** Gives the views their context. Called before the first frame, and for a new screen. */
    virtual void setContext(UiContext& context) = 0;

    /** Lays out the views of the page in the whole screen. Called each frame. */
    virtual void layout(UiLayoutContext& context) = 0;

    /** Stops the clocks of the page at now, while a dialog runs over it or the device sleeps. */
    virtual void pause(uint32_t) {}

    /** Continues the clocks of the page from now, after a dialog or a sleep. */
    virtual void resume(uint32_t) {}

    ModuleExitReason getExitReason() const { return exitReason; }

protected:
    ~UiPage() = default;

    /**
     * Requests the exit of the page at the end of this frame. The page still gets the
     * remaining events of the frame.
     */
    void exitTo(ModuleExitReason reason) { exitReason = reason; }

private:
    ModuleExitReason exitReason = MODULE_EXIT_NONE;
};

/** Runs the frame loop of the page until it requests an exit, and returns the reason. */
ModuleExitReason runPageLoop(UiPage& page, DisplayContext* display);

#endif
