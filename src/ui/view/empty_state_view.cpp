#include "ui/view/empty_state_view.hpp"

#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

void EmptyStateView::IconView::draw(UiDrawContext& context) {
    dirty                = false;
    SDL_Surface* surface = context.getSurface();
    SDL_Surface* bitmap  = Icons_getEmpty(THEME_ROLE_PRIMARY, false);
    if (!surface || !bitmap) return;
    SDL_SetSurfaceBlendMode(bitmap, SDL_BLENDMODE_BLEND);
    SDL_BlitScaled(bitmap, nullptr, surface, nullptr);
}

EmptyStateView::EmptyStateView() : message(Fonts_getMedium(), true), line(Fonts_getSmall(), true) {}

void EmptyStateView::onAttach(UiContext&) {
    message.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
    line.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
    icon.invalidate();
}

void EmptyStateView::layout(UiLayoutContext& context) {
    const UiSize space        = context.getMaxSize();
    const int iconSize        = SCALE1(48);
    const bool hasIcon        = Icons_getEmpty(THEME_ROLE_PRIMARY, false) != nullptr;
    UiLayoutContext measuring = context.createMeasuringContext();
    const UiSize messageSize  = measuring.layoutChild(message, SDL_Rect{0, 0, space.w, space.h}).getSize();
    const UiSize lineSize     = measuring.layoutChild(line, SDL_Rect{0, 0, space.w, space.h}).getSize();

    // The icon moves the texts down by half of its height
    const int messageY = space.h / 2 - SCALE1(25) + (hasIcon ? iconSize / 2 : 0);
    icon.setVisible(hasIcon);
    if (hasIcon) {
        context.layoutChild(icon, SDL_Rect{(space.w - iconSize) / 2, messageY - SCALE1(54), iconSize, iconSize});
    }
    // The same width as the measure, thus a text with an ellipsis does not render again
    context.layoutChild(message, SDL_Rect{(space.w - messageSize.w) / 2, messageY, space.w, messageSize.h});
    context.layoutChild(line, SDL_Rect{(space.w - lineSize.w) / 2, messageY + SCALE1(20), space.w, lineSize.h});
    context.setSize(space);
}
