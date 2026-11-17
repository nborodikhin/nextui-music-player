#ifndef __UI_VIEW_LIST_ITEM_VIEW_HPP__
#define __UI_VIEW_LIST_ITEM_VIEW_HPP__

#include "ui/view/ui_view.hpp"

/** The view of an item of a list. The list sets its selection. */
class ListItemView : public UiView {
public:
    virtual void setSelected(bool selected) = 0;

    /**
     * Moves the text of the item at the scrolling speed of the app, where the item shows it, until
     * stopScrolling(). An item with no moving text ignores these calls.
     */
    virtual void startScrolling() {}
    virtual void stopScrolling() {}
    /** Puts the text of the item at its start. */
    virtual void resetScrolling() {}
};

#endif
