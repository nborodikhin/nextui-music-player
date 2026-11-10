#include "ui/view/scroll_indicator_view.hpp"

#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

UiSize ScrollIndicatorView::getIndicatorSize() {
    // The scroll assets of the platform are 24 by 6 units
    return UiSize{SCALE1(24), SCALE1(6)};
}

void ScrollIndicatorView::layout(UiLayoutContext& context) {
    const UiSize size = getIndicatorSize();
    context.setSize(size);
}

void ScrollIndicatorView::draw(UiDrawContext& context) {
    const UiSize size  = getIndicatorSize();
    SDL_Surface* shape = SDL_CreateRGBSurfaceWithFormat(0, size.w, size.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!shape) return;
    SDL_FillRect(shape, nullptr, 0);
    SDL_Rect at = {0, 0, 0, 0};
    GFX_blitAsset(direction == Direction::Up ? ASSET_SCROLL_UP : ASSET_SCROLL_DOWN, nullptr, shape, &at);

    // The asset is a dark shape, thus a tint can only make it darker. Its alpha is the shape,
    // and the color of the secondary role fills it.
    const uint32_t rgb = Theme_getPackedColor(THEME_ROLE_SECONDARY, false) & 0x00FFFFFFu;
    for (int y = 0; y < shape->h; y++) {
        uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(shape->pixels) + y * shape->pitch);
        for (int x = 0; x < shape->w; x++) row[x] = (row[x] & 0xFF000000u) | rgb;
    }
    SDL_SetSurfaceBlendMode(shape, SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(shape, nullptr, context.getSurface(), nullptr);
    SDL_FreeSurface(shape);
}
