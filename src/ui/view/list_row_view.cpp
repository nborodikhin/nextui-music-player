#include "ui/view/list_row_view.hpp"

#include <algorithm>

#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

ListRowView::ListRowView(TTF_Font* font) : font(font), shortText(font, true), longText(font, false) {
    shortText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
    longText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
}

void ListRowView::setText(const char* text) {
    shortText.setText(text);
    longText.setText(text);
}

void ListRowView::setSelected(bool newSelected) {
    if (selected == newSelected) return;
    selected = newSelected;
    shortText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, selected));
    longText.setColor(Theme_getColor(THEME_ROLE_PRIMARY, selected));
    if (!selected) longText.updateScrolling(0);
    dirty = true;
}

void ListRowView::updateScrolling(uint32_t elapsedMs) { longText.updateScrolling(elapsedMs); }

void ListRowView::setContext(UiContext& context) {
    UiView::setContext(context);
    shortText.setContext(context);
    longText.setContext(context);
    dirty = true;
}

void ListRowView::layout(UiLayoutContext& context) {
    // A row is as high as a selection pill
    const UiSize size       = {context.getMaxSize().w, std::min(context.getMaxSize().h, SCALE1(PILL_SIZE))};
    const int padding       = SCALE1(BUTTON_PADDING);
    const int textHeight    = TTF_FontHeight(font);
    const SDL_Rect textRect = {padding, (size.h - textHeight) / 2, size.w - padding * 2, textHeight};

    // Both texts know their space, thus the long text can start to scroll when the row is selected
    UiLayoutContext measuring = context.createMeasuringContext();
    measuring.layoutChild(shortText, textRect);
    measuring.layoutChild(longText, textRect);
    const bool clipped = shortText.isClipped();
    TextView& shown    = selected && clipped ? longText : shortText;
    context.setSize(size);

    // A text that does not fit takes the whole row, and the pill with it
    const int newPillWidth = clipped ? size.w : std::min(size.w, shortText.getSize().w + padding * 2);
    if (newPillWidth != pillWidth || size.h != pillHeight) {
        pillWidth  = newPillWidth;
        pillHeight = size.h;
        dirty      = true;
    }
    context.layoutChild(shown, textRect);
}

void ListRowView::draw(UiDrawContext& context) {
    dirty = false;
    if (!selected) return;
    SDL_Rect pill = {0, 0, pillWidth, pillHeight};
    context.setContentRect(pill);
    // The fill argument marks a pill with no separate center color
    //noinspection HardcodedColor
    GFX_blitPillColor(ASSET_WHITE_PILL, context.getSurface(), &pill,
                      Theme_getPackedColor(THEME_ROLE_PAGE_BACKGROUND, true), RGB_WHITE);
}
