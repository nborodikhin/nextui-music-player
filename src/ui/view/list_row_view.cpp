#include "ui/view/list_row_view.hpp"

#include <algorithm>
#include <utility>

#include "ui_fonts.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

ListRowView::ListRowView() : shortText(Fonts_getLarge(), true), longText(Fonts_getLarge(), false) {
    icon.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
    shortText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
    longText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
}

bool ListRowView::setText(const std::string& text) {
    shortText.setText(text);
    return longText.setText(text);
}

void ListRowView::setSelected(bool newSelected) {
    if (selected == newSelected) return;
    selected = newSelected;
    shortText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, selected));
    longText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, selected));
    icon.setColor(Theme_getColor(THEME_ROLE_PRIMARY, selected));
    if (!selected) {
        stopScrolling();
        resetScrolling();
    }
    dirty = true;
}

void ListRowView::setLeftIcon(IconId id) {
    icon.setIcon(id);
    hasIcon = true;
    dirty   = true;
}

void ListRowView::setRightView(std::shared_ptr<UiView> view) {
    if (view == rightView) return;
    rightView = std::move(view);
    dirty = true;
}

void ListRowView::layout(UiLayoutContext& context) {
    // A row is as high as a selection pill
    const UiSize size       = {context.getMaxSize().w, std::min(context.getMaxSize().h, SCALE1(PILL_SIZE))};
    const int padding   = SCALE1(BUTTON_PADDING);
    const int iconSize  = SCALE1(24);
    const int iconSpace = hasIcon ? iconSize + SCALE1(6) : 0;
    SDL_Rect textRect   = {padding + iconSpace, 0, size.w - padding * 2 - iconSpace, size.h};

    UiLayoutContext measuring = context.createMeasuringContext();
    SDL_Rect rightRect{};
    if (rightView) {
        const UiSize rightSize = measuring.layoutChild(*rightView, textRect).getSize();
        rightRect  = {size.w - padding - rightSize.w, (size.h - rightSize.h) / 2, rightSize.w, rightSize.h};
        // The text ends one padding before the right view, as the pill around it does
        textRect.w = std::max(0, rightRect.x - padding * 2 - textRect.x);
    }

    // Both texts know their space, thus the long text can start to scroll when the row is selected
    const int textHeight      = measuring.layoutChild(shortText, textRect).getSize().h;
    measuring.layoutChild(longText, textRect);
    textRect.y = (size.h - textHeight) / 2;
    textRect.h = textHeight;
    const bool clipped      = shortText.isClipped();
    const bool showLongText = selected && clipped;
    // The whole text lost its frame callback while the row showed the short text
    if (showLongText && !longTextShown && scrollRequested) longText.startScrolling();
    longTextShown   = showLongText;
    TextView& shown = showLongText ? longText : shortText;
    context.setSize(size);

    // A text that does not fit takes all of its space, and the pill with it
    const int textSpace    = textRect.w + padding * 2 + iconSpace;
    const int newPillWidth = clipped ? textSpace : std::min(textSpace, shortText.getSize().w + padding * 2 + iconSpace);
    const int newBandWidth = rightView ? size.w : 0;
    if (newPillWidth != pillWidth || size.h != pillHeight || newBandWidth != bandWidth) {
        pillWidth  = newPillWidth;
        pillHeight = size.h;
        bandWidth  = newBandWidth;
        dirty      = true;
    }
    if (hasIcon) context.layoutChild(icon, SDL_Rect{padding, (size.h - iconSize) / 2, iconSize, iconSize});
    context.layoutChild(shown, textRect);
    if (rightView) context.layoutChild(*rightView, rightRect);
}

void ListRowView::draw(UiDrawContext& context) {
    dirty = false;
    if (!selected) return;
    SDL_Rect pill = {0, 0, pillWidth, pillHeight};
    if (bandWidth > 0) {
        SDL_Rect band = {0, 0, bandWidth, pillHeight};
        context.setContentRect(band);
        // The fill argument marks a pill with no separate center color
        //noinspection HardcodedColor
        GFX_blitPillColor(ASSET_WHITE_PILL, context.getSurface(), &band,
                          Theme_getPackedColor(THEME_ROLE_BAND_BACKGROUND, false), RGB_WHITE);
    } else {
        context.setContentRect(pill);
    }
    // The fill argument marks a pill with no separate center color
    //noinspection HardcodedColor
    GFX_blitPillColor(ASSET_WHITE_PILL, context.getSurface(), &pill,
                      Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true), RGB_WHITE);
}
