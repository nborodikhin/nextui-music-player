#include "ui/view/platform_group_view.hpp"

#include <algorithm>
#include <utility>

PlatformGroupView::~PlatformGroupView() {
    if (scratch) SDL_FreeSurface(scratch);
}

void PlatformGroupView::setContext(UiContext& context) {
    UiView::setContext(context);
    if (scratch) SDL_FreeSurface(scratch);
    const UiSize size = context.getScreenSize();
    scratch           = SDL_CreateRGBSurfaceWithFormat(0, size.w, size.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (scratch) SDL_FillRect(scratch, nullptr, 0);
    scratchRect = {0, 0, 0, 0};
    renderedPixels.clear();
    dirty       = true;
    needsRender = true;
}

SDL_Surface* PlatformGroupView::clearScratch() {
    // The helpers of the platform blend onto their destination, thus each render starts clear.
    // The scratch is transparent outside the last render.
    if (scratch) {
        SDL_Rect last = scratchRect;
        SDL_FillRect(scratch, &last, 0);
    }
    return scratch;
}

void PlatformGroupView::finishRender(const SDL_Rect& rect) {
    SDL_Rect bounds = {0, 0, scratch ? scratch->w : 0, scratch ? scratch->h : 0};
    SDL_Rect inside = {0, 0, 0, 0};
    SDL_IntersectRect(&rect, &bounds, &inside);
    std::vector<uint32_t> pixels(static_cast<size_t>(inside.w) * static_cast<size_t>(inside.h));
    for (int y = 0; y < inside.h; y++) {
        const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(scratch->pixels) +
                                                            (inside.y + y) * scratch->pitch) +
                          inside.x;
        std::copy(row, row + inside.w, pixels.begin() + static_cast<long>(y) * inside.w);
    }
    const bool sameRect = rect.x == scratchRect.x && rect.y == scratchRect.y && rect.w == scratchRect.w &&
                          rect.h == scratchRect.h;
    if (!sameRect || pixels != renderedPixels) dirty = true;
    scratchRect    = rect;
    renderedPixels = std::move(pixels);
}

void PlatformGroupView::draw(UiDrawContext& context) {
    dirty = false;
    if (!scratch) return;
    // The scratch holds premultiplied pixels, thus the copy takes no blend
    SDL_SetSurfaceBlendMode(scratch, SDL_BLENDMODE_NONE);
    SDL_Rect from = scratchRect;
    SDL_Rect to   = {0, 0, 0, 0};
    SDL_BlitSurface(scratch, &from, context.getSurface(), &to);
}
