#ifndef __UI_PAGE_PAGE_CONTEXT_HPP__
#define __UI_PAGE_PAGE_CONTEXT_HPP__

#include <cstdint>
#include <memory>
#include <vector>

#include "ui/view/ui_context.hpp"

/**
 * The context of one page: the facts of the screen, the two times and the tasks of the page and
 * its views. The stack starts each frame of the page, and runs the tasks of the frame after the
 * input of the frame.
 */
class PageContext : public UiContext {
public:
    /** Returns a new context that reads the facts of `screen`. */
    static std::unique_ptr<PageContext> create(const UiScreenInfo& screen);

    virtual ~PageContext() = default;

    /** Takes the facts of a new screen. Keeps the times and the tasks. */
    virtual void setScreen(const UiScreenInfo& screen) = 0;

    /** Starts a frame at these times. */
    virtual void startFrame(uint32_t elapsedTime, uint32_t activeTime) = 0;

    /** Runs the frame callbacks, then each task that expired at the active time of the frame. */
    virtual void runFrameTasks() = 0;

    /** Cancels each task whose owner is not in `owners`. */
    virtual void updateActiveOwners(const std::vector<TaskOwner>& owners) = 0;

    /** Starts a layout of the page. Each view that the layout lays out or measures stays attached. */
    virtual void startLayout() = 0;

    /** Ends the layout of the page, and detaches each view that the layout did not reach. */
    virtual void finishLayout() = 0;
};

#endif
