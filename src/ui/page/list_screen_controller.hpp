#ifndef __UI_PAGE_LIST_SCREEN_CONTROLLER_HPP__
#define __UI_PAGE_LIST_SCREEN_CONTROLLER_HPP__

#include <cstdint>
#include <span>

#include "input/input_events.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/view/ui_task_scheduler.hpp"

enum class HorizontalDirection : uint8_t { Left, Right };

/** What the page of a list screen decides for its list. */
class ListScreenListener {
public:
    /**
     * Called for each Left and Right while the list has a selection. `isRepeat` is true when the
     * user holds the button.
     * Returns: true when the page handles the button, else the list moves one page. The default
     * returns false.
     */
    virtual bool onLeftRight([[maybe_unused]] int listItemPosition, [[maybe_unused]] HorizontalDirection direction,
                             [[maybe_unused]] bool isRepeat) {
        return false;
    }

    /** Called with the position of the selection when the selected item or its position changes. */
    virtual void onSelectionChanged(int) {}

protected:
    ~ListScreenListener() = default;
};

/**
 * The cursor buttons of a list screen, and the waits before its title and its selected row scroll.
 * The page gives the list its adapter, and calls each change of the items through
 * notifyDataChanged().
 */
class ListScreenController {
public:
    ListScreenController(ListScreenView& view, UiTaskScheduler& scheduler, ListScreenListener& listener);

    /** Returns the repeats of the cursor buttons. A page with more requests adds these to its own. */
    static std::span<const input::ButtonRequest> getButtonRequests();

    /** Starts the waits before the title and the selected row scroll, and gives the first selection. */
    void start();

    /**
     * Handles Up and Down, and Left and Right, which go to onLeftRight() first. The page calls it
     * before it handles its own buttons.
     * Returns: true for a cursor button
     */
    bool onButtonEvent(const input::ButtonEvent& event);

    /** Reads the items of the list again. */
    void notifyDataChanged();

    /** Returns the position of the selected item, or -1 for an empty list. */
    int getSelectedIndex() const;

    /**
     * Selects the item at `index`, which the next layout scrolls into view. A new selection
     * restarts the wait before its row scrolls, and goes to onSelectionChanged().
     */
    void setSelectedIndex(int index);

    int getItemCount() const;
    ListItemView& getItemView(int index);

    /** Puts the text of the selected row at rest, and starts the wait before it scrolls. */
    void restartRowScrolling();

private:
    /** Restarts the row and tells the listener when the selection is not the one it knows. */
    void showSelection();

    ListScreenView& view;
    UiTaskScheduler& scheduler;
    ListScreenListener& listener;
    /** The selection that the listener knows */
    int shownIndex = -1;
    long shownId   = 0;
    TaskId titleStart = 0;
    TaskId rowStart   = 0;
};

#endif
