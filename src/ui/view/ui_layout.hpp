#ifndef __UI_VIEW_UI_LAYOUT_HPP__
#define __UI_VIEW_UI_LAYOUT_HPP__

#include "ui/view/ui_view.hpp"

/**
 * A composite view that lays out several child views.
 * It does not have any visual content of its own. Common use cases (e.g. custom
 * background) should be implemented using a child view.
 */
class UiLayout : public UiView {
public:
    void layout(UiLayoutContext&) override = 0;
    bool isDirty() const final { return false; }
    void draw(UiDrawContext&) final {}
};

#endif
