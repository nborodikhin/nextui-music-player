#ifndef __UI_VIEW_BUTTON_HINTS_VIEW_HPP__
#define __UI_VIEW_BUTTON_HINTS_VIEW_HPP__

#include "input/input_state.hpp"
#include "ui/view/platform_group_view.hpp"

/**
 * The button hints of a page in the bottom pill row. While a hardware bar shows, the hints of
 * the platform take the place of the left group.
 */
class ButtonHintsView : public PlatformGroupView {
public:
    /** Takes the null-terminated arrays of GFX_blitButtonGroup(). The arrays stay with the caller. */
    void setHints(const char* const* leftHints, const char* const* rightHints);

    void layout(UiLayoutContext& context) override;

private:
    const char* const* left        = nullptr;
    const char* const* right       = nullptr;
    input::HardwareBar renderedBar = input::HardwareBar::None;
};

#endif
