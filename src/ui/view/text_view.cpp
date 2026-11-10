#include "ui/view/text_view.hpp"

#include <algorithm>
#include <vector>

#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

TextView::TextView(TTF_Font* font, bool useEllipsis) : font(font), useEllipsis(useEllipsis) {}

TextView::~TextView() { freeRender(); }

void TextView::setText(const char* newText) {
    const char* value = newText ? newText : "";
    if (text == value) return;
    text        = value;
    renderStale = true;
    dirty       = true;
}

void TextView::setColor(SDL_Color newColor) {
    if (newColor.r == color.r && newColor.g == color.g && newColor.b == color.b && newColor.a == color.a) {
        return;
    }
    color = newColor;
    dirty = true;
}

bool TextView::isClipped() const { return maxWidth > 0 && textWidth > maxWidth; }

void TextView::updateScrolling(uint32_t elapsedMs) {
    if (useEllipsis || !isClipped()) {
        if (scrolling || offset != 0) {
            scrolling = false;
            offset    = 0;
            dirty     = true;
        }
        return;
    }
    const uint64_t distance   = static_cast<uint64_t>(elapsedMs) * SCALE1(MARQUEE_SPEED) / 1000;
    const int newOffset       = static_cast<int>(distance % static_cast<uint64_t>(textWidth + SCALE1(SCROLL_GAP)));
    const bool newScrolling   = elapsedMs > 0;
    if (newOffset == offset && newScrolling == scrolling) return;
    offset    = newOffset;
    scrolling = newScrolling;
    dirty     = true;
}

void TextView::setContext(UiContext& context) {
    UiView::setContext(context);
    renderStale = true;
    dirty       = true;
}

int TextView::getFontHeight() const { return TTF_FontHeight(font); }

void TextView::freeRender() {
    if (bitmap) SDL_FreeSurface(bitmap);
    bitmap = nullptr;
}

void TextView::render() {
    freeRender();
    renderStale      = false;
    renderedMaxWidth = maxWidth;
    textWidth        = 0;
    if (text.empty()) return;
    TTF_SizeUTF8(font, text.c_str(), &textWidth, nullptr);

    // The text is white, thus the color mod of the draw gives it any color
    //noinspection HardcodedColor
    const SDL_Color white = {255, 255, 255, 255};
    if (useEllipsis && textWidth > maxWidth) {
        std::vector<char> truncated(text.size() + 1);
        UI_truncateText(font, text.c_str(), truncated.data(), maxWidth, 0);
        bitmap = TTF_RenderUTF8_Blended(font, truncated.data(), white);
    } else {
        bitmap = TTF_RenderUTF8_Blended(font, text.c_str(), white);
    }
}

void TextView::layout(UiLayoutContext& context) {
    maxWidth = context.getMaxSize().w;
    if (renderStale || (useEllipsis && maxWidth != renderedMaxWidth)) {
        render();
        dirty = true;
    }
    const int width  = std::min(useEllipsis && bitmap ? bitmap->w : textWidth, maxWidth);
    const int height = getFontHeight();
    context.setSize(UiSize{width, height});
    context.setTargetLayer(scrolling && isClipped() ? UiTargetLayer::AnimationLayer
                                                    : UiTargetLayer::MainSurface);
}

void TextView::draw(UiDrawContext& context) {
    dirty               = false;
    SDL_Surface* target = context.getSurface();
    if (!bitmap) return;
    SDL_SetSurfaceBlendMode(bitmap, SDL_BLENDMODE_BLEND);
    // The text is opaque, as a text that TTF renders in a role color is
    SDL_SetSurfaceColorMod(bitmap, color.r, color.g, color.b);

    SDL_Rect from = {offset, 0, bitmap->w - offset, bitmap->h};
    SDL_Rect to   = {0, 0, 0, 0};
    SDL_BlitSurface(bitmap, &from, target, &to);
    // The text loops: its next copy follows the gap
    const int next = bitmap->w + SCALE1(SCROLL_GAP) - offset;
    if (offset > 0 && next < target->w) {
        SDL_Rect whole = {0, 0, bitmap->w, bitmap->h};
        SDL_Rect at    = {next, 0, 0, 0};
        SDL_BlitSurface(bitmap, &whole, target, &at);
    }
    finishDraw(target);
}
