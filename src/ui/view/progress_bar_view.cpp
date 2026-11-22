#include "ui/view/progress_bar_view.hpp"

#include <algorithm>
#include <cstdio>

#include "ui_theme.h"

void ProgressTrackView::setPercent(int newPercent) {
    newPercent = std::clamp(newPercent, 0, 100);
    if (newPercent == percent) return;
    percent = newPercent;
    dirty   = true;
}

void ProgressTrackView::draw(UiDrawContext& context) {
    dirty                = false;
    SDL_Surface* surface = context.getSurface();
    if (!surface) return;
    const auto fill = [surface](const SDL_Rect& rect, SDL_Color color) {
        SDL_FillRect(surface, &rect, SDL_MapRGBA(surface->format, color.r, color.g, color.b, 255));
    };
    fill(SDL_Rect{0, 0, surface->w, surface->h}, Theme_getColor(THEME_ROLE_PROGRESS_TRACK, false));
    const int fillWidth = surface->w * percent / 100;
    if (fillWidth > 0) fill(SDL_Rect{0, 0, fillWidth, surface->h}, Theme_getColor(THEME_ROLE_PROGRESS_FILL, false));
}

ProgressBarView::ProgressBarView(TTF_Font* font, int barHeight) : barHeight(barHeight), label(font, false) {
    label.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
    label.setText("0%");
}

void ProgressBarView::setPercent(int percent) {
    track.setPercent(percent);
    char text[8];
    snprintf(text, sizeof(text), "%d%%", track.getPercent());
    label.setText(text);
}

void ProgressBarView::layout(UiLayoutContext& context) {
    const UiSize space        = context.getMaxSize();
    UiLayoutContext measuring = context.createMeasuringContext();
    const UiSize labelSize    = measuring.layoutChild(label, SDL_Rect{0, 0, space.w, space.h}).getSize();
    const int height          = std::max(barHeight, labelSize.h);
    context.setSize(UiSize{space.w, height});
    context.layoutChild(track, SDL_Rect{0, (height - barHeight) / 2, space.w, barHeight});
    context.layoutChild(label, SDL_Rect{(space.w - labelSize.w) / 2, (height - labelSize.h) / 2, labelSize.w,
                                        labelSize.h});
}
