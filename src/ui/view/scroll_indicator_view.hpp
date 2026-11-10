#ifndef __UI_VIEW_SCROLL_INDICATOR_VIEW_HPP__
#define __UI_VIEW_SCROLL_INDICATOR_VIEW_HPP__

#include "ui/view/ui_view.hpp"

/** A small arrow above or below a list that shows that more rows are in that direction. */
class ScrollIndicatorView : public UiView {
public:
    enum class Direction { Up, Down };

    explicit ScrollIndicatorView(Direction direction) : direction(direction) {}

    void layout(UiLayoutContext& context) override;
    void draw(UiDrawContext& context) override;

private:
    static UiSize getIndicatorSize();

    Direction direction;
};

#endif
