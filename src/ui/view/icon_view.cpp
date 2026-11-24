#include "ui/view/icon_view.hpp"

void IconView::setIcon(IconId newIcon) {
    if (icon == newIcon) return;
    icon  = newIcon;
    dirty = true;
}

void IconView::setColor(SDL_Color newColor) {
    if (newColor.r == color.r && newColor.g == color.g && newColor.b == color.b && newColor.a == color.a) return;
    color = newColor;
    dirty = true;
}

void IconView::onAttach(UiContext&) { dirty = true; }

void IconView::layout(UiLayoutContext& context) { context.setSize(context.getMaxSize()); }

void IconView::draw(UiDrawContext& context) {
    dirty                = false;
    SDL_Surface* surface = context.getSurface();
    // The icon is a shared surface, which is valid only until the next request of that icon
    SDL_Surface* bitmap = Icons_getIcon(icon, color);
    if (!surface || !bitmap) return;
    SDL_SetSurfaceBlendMode(bitmap, SDL_BLENDMODE_BLEND);
    SDL_BlitScaled(bitmap, nullptr, surface, nullptr);
}
