#include "ui/view/text_view.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
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
    color       = newColor;
    renderStale = true;
    dirty       = true;
}

bool TextView::isClipped() const { return maxWidth > 0 && textWidth > maxWidth; }

void TextView::startScrolling(int32_t spPerSecond) {
    if (spPerSecond <= 0) {
        stopScrolling();
        return;
    }
    speedSpPerSecond = spPerSecond;
    if (!hasContext()) {
        startPending = true;
        return;
    }
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
    startPending     = false;
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

void TextView::onAttach(UiContext&) {
    renderStale = true;
    dirty       = true;
    // Only a start from before the attach starts the scroll. A view that comes back to the layout
    // scrolls again after a new start only.
    if (std::exchange(startPending, false)) startScrolling(speedSpPerSecond);
}

int TextView::getFontHeight() const { return TTF_FontHeight(font); }

void TextView::freeRender() {
    if (bitmap) SDL_FreeSurface(bitmap);
    bitmap = nullptr;
}

// Converts `surface` to premultiplied ARGB8888, the form of a bitmap of a view. Frees `surface`.
static SDL_Surface* toPremultiplied(SDL_Surface* surface) {
    if (!surface) return nullptr;
    SDL_Surface* converted = surface;
    if (surface->format->format != SDL_PIXELFORMAT_ARGB8888) {
        converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(surface);
        if (!converted) return nullptr;
    }
    for (int y = 0; y < converted->h; y++) {
        uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(converted->pixels) + y * converted->pitch);
        for (int x = 0; x < converted->w; x++) {
            const uint32_t alpha = row[x] >> 24;
            if (alpha == 255) continue;
            uint32_t pixel = alpha << 24;
            for (int shift = 0; shift < 24; shift += 8) {
                pixel |= ((((row[x] >> shift) & 0xFF) * alpha + 127) / 255) << shift;
            }
            row[x] = pixel;
        }
    }
    // The bitmap of the view is empty under the text, thus a copy gives the same pixels as a blend
    SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_NONE);
    return converted;
}

SDL_Surface* TextView::renderBitmap(const std::string& line) const {
    const SDL_Color opaque = {color.r, color.g, color.b, 255};
    return toPremultiplied(TTF_RenderUTF8_Blended(font, line.c_str(), opaque));
}

void TextView::render() {
    freeRender();
    renderStale      = false;
    renderedMaxWidth = maxWidth;
    textWidth        = 0;
    if (text.empty()) return;
    TTF_SizeUTF8(font, text.c_str(), &textWidth, nullptr);

    const SDL_Color opaque = {color.r, color.g, color.b, 255};
    if (useEllipsis && textWidth > maxWidth) {
        std::vector<char> truncated(text.size() + 1);
        UI_truncateText(font, text.c_str(), truncated.data(), maxWidth, 0);
        bitmap = toPremultiplied(TTF_RenderUTF8_Blended(font, truncated.data(), opaque));
    } else {
        bitmap = toPremultiplied(TTF_RenderUTF8_Blended(font, text.c_str(), opaque));
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
    if (!bitmap || !target) return;
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
