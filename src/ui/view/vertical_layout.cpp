#include "ui/view/vertical_layout.hpp"

#include <algorithm>

void VerticalLayout::addChild(std::unique_ptr<UiView> child) {
    children.push_back(std::move(child));
}

void VerticalLayout::layout(UiLayoutContext& context) {
    const UiSize maxSize = context.getMaxSize();
    int y                = 0;
    for (const std::unique_ptr<UiView>& child : children) {
        const SDL_Rect room = {0, y, maxSize.w, std::max(0, maxSize.h - y)};
        y += context.layoutChild(*child, room).getSize().h;
    }
    context.setSize(UiSize{maxSize.w, y});
}
