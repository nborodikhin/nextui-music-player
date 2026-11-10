#include "ui/view/button_hints_view.hpp"

#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using input::HardwareBar;
using input::InputState;

void ButtonHintsView::setHints(const char* const* leftHints, const char* const* rightHints) {
    if (leftHints == left && rightHints == right) return;
    left        = leftHints;
    right       = rightHints;
    needsRender = true;
}

void ButtonHintsView::layout(UiLayoutContext& context) {
    const HardwareBar bar = InputState::getInstance().getHardwareBar();
    if (needsRender || bar != renderedBar) {
        SDL_Surface* scratch = clearScratch();
        if (scratch) {
            if (bar != HardwareBar::None) {
                GFX_blitHardwareHints(scratch, static_cast<int>(bar));
            } else if (left) {
                blit_button_group(left, false, scratch, false);
            }
            if (right) blit_button_group(right, true, scratch, true);
            const int height = SCALE1(PILL_SIZE);
            finishRender(SDL_Rect{0, scratch->h - SCALE1(PADDING) - height, scratch->w, height});
        }
        needsRender = false;
        renderedBar = bar;
    }
    const SDL_Rect& rect = getScratchRect();
    context.setSize(UiSize{rect.w, rect.h});
}
