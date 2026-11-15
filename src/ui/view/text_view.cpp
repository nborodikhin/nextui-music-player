#include "ui/view/text_view.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

TextView::TextView(TTF_Font* font, bool useEllipsis) : font(font), useEllipsis(useEllipsis) {}

TextView::~TextView() { freeRender(); }

bool TextView::setText(const std::string& newText) {
    if (text == newText) return false;
    text        = newText;
    renderStale = true;
    dirty       = true;
    resetScrolling();
    return true;
}

void TextView::setColor(SDL_Color newColor) {
    if (newColor.r == color.r && newColor.g == color.g && newColor.b == color.b && newColor.a == color.a) {
        return;
    }
    color = newColor;
    dirty = true;
}

bool TextView::isClipped() const { return maxWidth > 0 && textWidth > maxWidth; }

void TextView::startScrolling(int32_t spPerSecond) {
    if (spPerSecond <= 0) {
        stopScrolling();
        return;
    }
    speedSpPerSecond = spPerSecond;
    if (!hasContext()) return;
    // The new speed counts from now, also for a text that moves
    stepTime = getContext().getActiveTime();
    if (!getScheduler().isTaskPending(scrollTask)) {
        scrollTask = getScheduler().runEachFrame([this] { stepScroll(); });
    }
    updateScrolling();
}

void TextView::startScrolling() { startScrolling(SCROLLING_SPEED_SP); }

void TextView::stopScrolling() {
    speedSpPerSecond = 0;
    getScheduler().cancelTask(scrollTask);
    scrollTask = 0;
    updateScrolling();
}

void TextView::resetScrolling() {
    setOffset(0);
    updateScrolling();
}

void TextView::updateScrolling() {
    const bool newScrolling = !useEllipsis && (speedSpPerSecond > 0 || offset > 0);
    if (newScrolling == scrolling) return;
    scrolling = newScrolling;
    dirty     = true;
}

void TextView::stepScroll() {
    const uint32_t now = getContext().getActiveTime();
    const uint32_t step = now - stepTime;
    stepTime = now;
    if (useEllipsis || !isClipped()) {
        setOffset(0);
        return;
    }
    const double loop = textWidth + SCALE1(SCROLL_GAP_SP);
    setOffset(std::fmod(exactOffset + static_cast<double>(SCALE1(speedSpPerSecond)) * step / 1000.0, loop));
}

void TextView::setOffset(double newOffset) {
    exactOffset     = newOffset;
    const int whole = static_cast<int>(newOffset);
    if (whole == offset) return;
    offset = whole;
    dirty  = true;
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
    const int width      = std::min(useEllipsis && bitmap ? bitmap->w : textWidth, maxWidth);
    const int fontHeight = getFontHeight();
    const int lineHeight = std::max(TTF_FontLineSkip(font), fontHeight);
    const int height     = std::max(fontHeight, std::min(lineHeight, context.getMaxSize().h));
    textTop              = (height - fontHeight) / 2;
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

    if (!target) return;
    context.setContentRect(SDL_Rect{0, textTop, target->w, bitmap->h});
    SDL_Rect from = {offset, 0, bitmap->w - offset, bitmap->h};
    SDL_Rect to   = {0, textTop, 0, 0};
    SDL_BlitSurface(bitmap, &from, target, &to);
    // The text loops: its next copy follows the gap
    const int next = bitmap->w + SCALE1(SCROLL_GAP_SP) - offset;
    if (offset > 0 && next < target->w) {
        SDL_Rect whole = {0, 0, bitmap->w, bitmap->h};
        SDL_Rect at    = {next, textTop, 0, 0};
        SDL_BlitSurface(bitmap, &whole, target, &at);
    }
    finishDraw(target);
}
