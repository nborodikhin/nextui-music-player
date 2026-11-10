#include "ui/view/ui_layout_context.hpp"

#include "ui/view/ui_view.hpp"

namespace {

SDL_Rect intersect(const SDL_Rect& a, const SDL_Rect& b) {
    SDL_Rect result;
    if (!SDL_IntersectRect(&a, &b, &result)) return SDL_Rect{a.x, a.y, 0, 0};
    return result;
}

}  // namespace

UiLayoutContext UiLayoutContext::makeRoot(UiSize screenSize, bool surfaceOnly) {
    UiLayoutContext root;
    root.maxSize     = screenSize;
    root.size        = screenSize;
    root.clipRect    = SDL_Rect{0, 0, screenSize.w, screenSize.h};
    root.surfaceOnly = surfaceOnly;
    return root;
}

UiLayoutContext& UiLayoutContext::layoutChild(UiView& childView, const SDL_Rect& rect) {
    return layoutChildIn(childView, rect, nullptr);
}

UiLayoutContext& UiLayoutContext::layoutChild(UiView& childView, const SDL_Rect& rect,
                                              const SDL_Rect& clip) {
    return layoutChildIn(childView, rect, &clip);
}

UiLayoutContext& UiLayoutContext::layoutChildIn(UiView& childView, const SDL_Rect& rect,
                                                const SDL_Rect* clip) {
    UiLayoutContext& child = children.emplace_back();
    child.view             = &childView;
    child.origin           = SDL_Point{origin.x + rect.x, origin.y + rect.y};
    child.maxSize          = UiSize{rect.w, rect.h};
    child.size             = child.maxSize;
    child.targetLayer      = targetLayer;
    child.surfaceOnly      = surfaceOnly;
    child.clipRect         = intersect(clipRect, getScreenRect());
    if (clip) {
        const SDL_Rect screenClip = {origin.x + clip->x, origin.y + clip->y, clip->w, clip->h};
        child.clipRect            = intersect(child.clipRect, screenClip);
    }
    childView.layout(child);
    childView.size = child.size;
    return child;
}

UiLayoutContext UiLayoutContext::createMeasuringContext() const {
    // The measuring context is not a child of this one, thus the compositor never reads it
    UiLayoutContext measuring;
    measuring.view        = view;
    measuring.origin      = origin;
    measuring.maxSize     = maxSize;
    measuring.size        = size;
    measuring.clipRect    = clipRect;
    measuring.targetLayer = targetLayer;
    measuring.surfaceOnly = surfaceOnly;
    return measuring;
}

void UiLayoutContext::setTargetLayer(UiTargetLayer target) {
    targetLayer = surfaceOnly ? UiTargetLayer::MainSurface : target;
}

SDL_Point UiLayoutContext::viewToScreen(SDL_Point point) const {
    return SDL_Point{point.x + origin.x, point.y + origin.y};
}

SDL_Point UiLayoutContext::screenToView(SDL_Point point) const {
    return SDL_Point{point.x - origin.x, point.y - origin.y};
}

SDL_Rect UiLayoutContext::getScreenRect() const {
    return SDL_Rect{origin.x, origin.y, size.w, size.h};
}
