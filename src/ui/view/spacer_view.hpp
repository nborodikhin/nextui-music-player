#ifndef __UI_VIEW_SPACER_VIEW_HPP__
#define __UI_VIEW_SPACER_VIEW_HPP__

#include "ui/view/ui_layout.hpp"

/** An empty space of a fixed height, as wide as the space that the parent gives. */
class SpacerView : public UiLayout {
public:
    explicit SpacerView(int height) : height(height) {}

    void layout(UiLayoutContext& context) override { context.setSize(UiSize{context.getMaxSize().w, height}); }

private:
    int height;
};

#endif
