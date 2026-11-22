#include "ui/view/screen_view.hpp"

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

SDL_Rect layoutScreenFrame(UiLayoutContext& context, UiView& titleView, UiView& statusView,
                           UiView& buttonHintsView) {
    const int padding = SCALE1(PADDING);
    const int top     = layoutScreenTop(context, titleView, statusView);
    const int bottom  = layoutScreenBottom(context, buttonHintsView);
    return SDL_Rect{padding, top, context.getMaxSize().w - padding * 2, bottom - top};
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

void ScreenView::layout(UiLayoutContext& context) {
    const int bodyTop    = layoutScreenTop(context, titleView, statusView);
    const int bodyBottom = layoutScreenBottom(context, buttonHintsView);
    if (content) context.layoutChild(*content, SDL_Rect{0, bodyTop, context.getMaxSize().w, bodyBottom - bodyTop});
}
