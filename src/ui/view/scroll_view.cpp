#include "ui/view/scroll_view.hpp"

#include <algorithm>
#include <array>

#include "ui_fonts.h"

using namespace input;

namespace {

/** The height that the content gets to measure itself, more than any content needs */
constexpr int kMeasureHeight = 1 << 20;

constexpr std::array<ButtonRequest, 4> kRequests = {
    request::Repeat{Button::DpadUp},
    request::Repeat{Button::DpadDown},
    request::Repeat{Button::DpadLeft},
    request::Repeat{Button::DpadRight},
};

}  // namespace

void ScrollView::setContent(UiView* newContent) {
    content = newContent;
    offset  = 0;
}

void ScrollView::scrollBy(int pixels) {
    offset = std::clamp(offset + pixels, 0, getMaxOffset());
}

void ScrollView::scrollPage(int pages) {
    scrollBy(pages * viewportHeight);
}

std::span<const ButtonRequest> ScrollView::getButtonRequests() {
    return kRequests;
}

bool ScrollView::handleButton(const ButtonEvent& event) {
    const int line = lineHeight > 0 ? lineHeight : Fonts_getMetric(Fonts_getMedium(), FONT_METRIC_LINE_HEIGHT);
    if (event.isSingleButton(Button::DpadUp)) {
        scrollBy(-line);
    } else if (event.isSingleButton(Button::DpadDown)) {
        scrollBy(line);
    } else if (event.isSingleButton(Button::DpadLeft)) {
        scrollPage(-1);
    } else if (event.isSingleButton(Button::DpadRight)) {
        scrollPage(1);
    } else {
        return false;
    }
    return true;
}

int ScrollView::getMaxOffset() const {
    return std::max(0, contentHeight - viewportHeight);
}

void ScrollView::layout(UiLayoutContext& context) {
    const UiSize size = context.getMaxSize();
    viewportHeight    = size.h;
    context.setSize(size);
    if (!content) {
        contentHeight = 0;
        offset        = 0;
        return;
    }

    UiLayoutContext measuring = context.createMeasuringContext();
    contentHeight = measuring.layoutChild(*content, SDL_Rect{0, 0, size.w, kMeasureHeight}).getSize().h;
    offset        = std::clamp(offset, 0, getMaxOffset());
    context.layoutChild(*content, SDL_Rect{0, -offset, size.w, contentHeight});
}
