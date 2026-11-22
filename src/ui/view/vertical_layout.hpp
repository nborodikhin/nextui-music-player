#ifndef __UI_VIEW_VERTICAL_LAYOUT_HPP__
#define __UI_VIEW_VERTICAL_LAYOUT_HPP__

#include <memory>
#include <vector>

#include "ui/view/ui_layout.hpp"

/**
 * Views from top to bottom. Each child gets the full width and the height that it needs. The
 * layout is as high as its children.
 */
class VerticalLayout : public UiLayout {
public:
    /** Adds `child` below the last child. */
    void addChild(std::unique_ptr<UiView> child);

    void layout(UiLayoutContext& context) override;

private:
    std::vector<std::unique_ptr<UiView>> children;
};

#endif
