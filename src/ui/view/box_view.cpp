#include "ui/view/box_view.hpp"

void BoxView::setColor(SDL_Color newColor) {
    if (newColor.r == color.r && newColor.g == color.g && newColor.b == color.b && newColor.a == color.a) return;
    color = newColor;
    dirty = true;
}

void BoxView::layout(UiLayoutContext& context) { context.setTargetLayer(target); }

void BoxView::draw(UiDrawContext& context) {
    dirty = false;
    if (color.a == 0) return;
    SDL_Surface* surface = context.getSurface();
    if (!surface) return;
    // The bitmap holds premultiplied alpha
    const auto premultiply = [this](Uint8 channel) { return static_cast<Uint8>((channel * color.a + 127) / 255); };
    SDL_FillRect(surface, nullptr,
                 SDL_MapRGBA(surface->format, premultiply(color.r), premultiply(color.g), premultiply(color.b),
                             color.a));
}
