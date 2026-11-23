#ifndef __UI_VIEW_SCROLLING_VIEW_HPP__
#define __UI_VIEW_SCROLLING_VIEW_HPP__

#include "ui/view/ui_layout.hpp"

/** A view that shows a part of a content that can be higher than the view. */
class ScrollingView : public UiLayout {
public:
    /** Returns true when a part of the content is above the part that shows. */
    virtual bool canScrollUp() const = 0;

    /** Returns true when a part of the content is below the part that shows. */
    virtual bool canScrollDown() const = 0;

    /** Returns the bottom of the part that shows, from the top of the view, after the layout. */
    virtual int getShownBottom() const = 0;
};

#endif
