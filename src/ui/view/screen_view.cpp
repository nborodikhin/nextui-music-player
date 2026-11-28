#include "ui/view/screen_view.hpp"

#include <utility>

#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

int layoutScreenTop(UiLayoutContext& context, UiView& titleView, UiView& statusView) {
    const UiSize screen   = context.getMaxSize();
    const int padding     = SCALE1(PADDING);
    const int pillHeight  = SCALE1(PILL_SIZE);
    const SDL_Rect topRow = {0, 0, screen.w, padding + pillHeight};

    // The status group draws first and gives its width, thus the title takes the room that is
    // left and never runs under it
    UiLayoutContext statusMeasuring = context.createMeasuringContext();
    const int statusWidth           = statusMeasuring.layoutChild(statusView, topRow).getSize().w;
    const SDL_Rect statusRect       = {screen.w - padding - statusWidth, padding, statusWidth, pillHeight};
    context.layoutChild(statusView, statusRect);

    // The same inset on each side, thus the title and the status group keep one gap
    const int titleX          = padding + SCALE1(BUTTON_PADDING);
    const int titleWidth      = screen.w - padding - SCALE1(BUTTON_PADDING) - statusWidth - titleX;
    UiLayoutContext measuring = context.createMeasuringContext();
    const SDL_Rect titleRoom  = {0, 0, titleWidth, pillHeight};
    const int titleHeight     = measuring.layoutChild(titleView, titleRoom).getSize().h;
    const int titleY          = padding + (pillHeight - titleHeight) / 2;
    context.layoutChild(titleView, SDL_Rect{titleX, titleY, titleWidth, titleHeight});
    return padding + pillHeight;
}

int layoutScreenBottom(UiLayoutContext& context, UiView& buttonHintsView) {
    const UiSize screen  = context.getMaxSize();
    const int pillHeight = SCALE1(PILL_SIZE);
    const int top        = screen.h - SCALE1(PADDING) - pillHeight;
    context.layoutChild(buttonHintsView, SDL_Rect{0, top, screen.w, pillHeight});
    return top;
}

int layoutCentered(UiLayoutContext& context, UiView& view, int y, int maxHeight) {
    const UiSize space        = context.getMaxSize();
    const int height          = maxHeight > 0 ? maxHeight : space.h;
    const int width           = space.w - 2 * SCALE1(PADDING);
    UiLayoutContext measuring = context.createMeasuringContext();
    const UiSize size         = measuring.layoutChild(view, SDL_Rect{0, 0, width, height}).getSize();
    // The same width as the measure, thus a text with an ellipsis does not render again. The view
    // takes its own width at the left of the rectangle.
    context.layoutChild(view, SDL_Rect{(space.w - size.w) / 2, y, width, size.h});
    return size.h;
}

ScreenView::ScreenView() : titleView(Fonts_getLarge()) {
    titleView.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
}

void ScreenView::start() {
    restartTitle();
}

void ScreenView::onPageFullyHidden() {
    restartTitle();
}

void ScreenView::onAttach(UiContext&) {
    if (std::exchange(startPending, false)) restartTitle();
}

void ScreenView::restartTitle() {
    titleView.restart();
    if (!hasContext()) {
        startPending = true;
        return;
    }
    getScheduler().cancelTask(titleStart);
    // The delay counts the active time of the page, thus it does not end while the page is covered
    titleStart = getScheduler().postDelayed([this] { titleView.startScrolling(); }, SCROLL_START_DELAY);
}

void ScreenView::setContent(UiView* newContent, ContentMargins newMargins) {
    content          = newContent;
    scrollingContent = nullptr;
    margins          = newMargins;
}

void ScreenView::setScrollingContent(ScrollingView* newContent, ContentMargins newMargins) {
    content          = newContent;
    scrollingContent = newContent;
    margins          = newMargins;
}

void ScreenView::layout(UiLayoutContext& context) {
    const int bodyTop    = layoutScreenTop(context, titleView, statusView);
    const int bodyBottom = layoutScreenBottom(context, buttonHintsView);
    if (!content) return;

    const int padding       = SCALE1(PADDING);
    const int side          = margins == ContentMargins::None ? 0 : padding;
    const int foot          = margins == ContentMargins::SidesAndFoot ? padding : 0;
    const SDL_Rect bodyRect = {side, bodyTop, context.getMaxSize().w - side * 2, bodyBottom - bodyTop - foot};
    context.layoutChild(*content, bodyRect);
    if (scrollingContent) layoutScrollIndicators(context, bodyRect);
}

void ScreenView::layoutScrollIndicators(UiLayoutContext& context, const SDL_Rect& contentRect) {
    // The down indicator sits under the part that shows, also over the button hints
    const SDL_Rect wholeScreen    = context.getMaxRect();
    UiLayoutContext upMeasuring   = context.createMeasuringContext();
    UiLayoutContext downMeasuring = context.createMeasuringContext();
    const UiSize upSize           = upMeasuring.layoutChild(scrollUpView, wholeScreen).getSize();
    const UiSize downSize         = downMeasuring.layoutChild(scrollDownView, wholeScreen).getSize();
    const int downY               = contentRect.y + scrollingContent->getShownBottom();
    context.layoutChild(scrollUpView,
                        SDL_Rect{(wholeScreen.w - upSize.w) / 2, contentRect.y - upSize.h, upSize.w, upSize.h});
    context.layoutChild(scrollDownView, SDL_Rect{(wholeScreen.w - downSize.w) / 2, downY, downSize.w, downSize.h});

    // The compositor reads the visibility after the layout
    scrollUpView.setVisible(scrollingContent->canScrollUp());
    scrollDownView.setVisible(scrollingContent->canScrollDown());
}
