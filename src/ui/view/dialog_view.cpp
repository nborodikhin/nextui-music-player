#include "ui/view/dialog_view.hpp"

#include <algorithm>

#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

namespace {

void fill(SDL_Surface* surface, const SDL_Rect& rect, SDL_Color color) {
    SDL_FillRect(surface, &rect, SDL_MapRGBA(surface->format, color.r, color.g, color.b, 255));
}

}  // namespace

void DialogFrameView::draw(UiDrawContext& context) {
    dirty                = false;
    SDL_Surface* surface = context.getSurface();
    if (!surface) return;
    const int w      = surface->w;
    const int h      = surface->h;
    const int border = SCALE1(2);
    fill(surface, SDL_Rect{0, 0, w, h}, Theme_getColor(THEME_ROLE_SURFACE_BACKGROUND, false));
    const SDL_Color edge = Theme_getColor(THEME_ROLE_PRIMARY, false);
    fill(surface, SDL_Rect{0, 0, w, border}, edge);
    fill(surface, SDL_Rect{0, h - border, w, border}, edge);
    fill(surface, SDL_Rect{0, 0, border, h}, edge);
    fill(surface, SDL_Rect{w - border, 0, border, h}, edge);
}

DialogView::DialogView(int width, Alignment alignment, TTF_Font* titleFont, TTF_Font* hintFont)
    : width(width), alignment(alignment), title(titleFont, true), hint(hintFont, true) {
    title.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
    hint.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
}

void DialogView::setContent(UiView* newContent, int height) {
    content       = newContent;
    contentHeight = height;
}

void DialogView::setContext(UiContext& context) {
    UiView::setContext(context);
    title.setContext(context);
    hint.setContext(context);
    if (content) content->setContext(context);
    frame.invalidate();
}

void DialogView::layout(UiLayoutContext& context) {
    const UiSize space        = context.getMaxSize();
    const int padding         = SCALE1(15);
    const int top             = SCALE1(12);
    const int gap             = SCALE1(10);
    UiLayoutContext measuring = context.createMeasuringContext();
    const int textWidth       = width - 2 * padding;
    const int titleHeight = measuring.layoutChild(title, SDL_Rect{0, 0, textWidth, space.h}).getSize().h;
    const int hintHeight  = measuring.layoutChild(hint, SDL_Rect{0, 0, textWidth, space.h}).getSize().h;
    const int bodyHeight  = content ? contentHeight + gap : 0;
    const int height      = top + titleHeight + gap + bodyHeight + hintHeight + gap;

    const SDL_Rect surface = {(space.w - width) / 2, (space.h - height) / 2, width, height};
    context.layoutChild(frame, surface);
    layoutText(context, measuring, title, surface, surface.y + top);
    int y = surface.y + top + titleHeight + gap;
    if (content) {
        context.layoutChild(*content, SDL_Rect{surface.x + padding, y, textWidth, contentHeight});
        y += contentHeight + gap;
    }
    layoutText(context, measuring, hint, surface, y);
}

void DialogView::layoutText(UiLayoutContext& context, UiLayoutContext& measuring, TextView& text,
                            const SDL_Rect& surface, int y) {
    const int padding   = SCALE1(15);
    const int textWidth = surface.w - 2 * padding;
    const UiSize size   = measuring.layoutChild(text, SDL_Rect{0, 0, textWidth, surface.h}).getSize();
    const int x = alignment == Alignment::Center ? surface.x + (surface.w - size.w) / 2 : surface.x + padding;
    // The same width as the measure, thus a text with an ellipsis does not render again
    context.layoutChild(text, SDL_Rect{x, y, textWidth, size.h});
}
