#ifndef __UI_VIEW_SCREEN_VIEW_HPP__
#define __UI_VIEW_SCREEN_VIEW_HPP__

#include <SDL2/SDL_ttf.h>

#include "ui/view/button_hints_view.hpp"
#include "ui/view/status_group_view.hpp"
#include "ui/view/title_text_view.hpp"
#include "ui/view/ui_layout.hpp"
#include "ui/view/ui_view.hpp"

/**
 * Lays out the top pill row of a screen: the status view at the right, and the title view in the
 * room that is left. Returns the height of the row.
 */
int layoutScreenTop(UiLayoutContext& context, UiView& titleView, UiView& statusView);

/** Lays out the button hints in the bottom pill row of a screen. Returns the top of the row. */
int layoutScreenBottom(UiLayoutContext& context, UiView& buttonHintsView);

/**
 * Lays out the frame of a full screen: the title view and the status view at the top, and the
 * button hints at the bottom. Returns the rectangle of the body between them.
 */
SDL_Rect layoutScreenFrame(UiLayoutContext& context, UiView& titleView, UiView& statusView,
                           UiView& buttonHintsView);

/**
 * Lays out `view` in its measured size, in the middle of the width of the context and inside its
 * side margins, with its top at `y`. Pass a height in `maxHeight` to limit the height of the
 * view, or 0 for no limit. A text view with the height of its font has no space above its text.
 * Returns the height of the view.
 */
int layoutCentered(UiLayoutContext& context, UiView& view, int y, int maxHeight = 0);

/**
 * A full-screen page: the title and the status group in the top pill row, the button hints in the
 * bottom pill row, and a content view in the body between them. It owns its views, but not the
 * content view.
 */
class ScreenView : public UiLayout {
public:
    explicit ScreenView(TTF_Font* titleFont) : titleView(titleFont) {}

    TitleTextView& getTitleView() { return titleView; }
    StatusGroupView& getStatusView() { return statusView; }
    ButtonHintsView& getButtonHintsView() { return buttonHintsView; }

    /** Lays out `content` in the body. The view stays with the caller. Pass null for no content. */
    void setContent(UiView* content) { this->content = content; }

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;

private:
    TitleTextView titleView;
    StatusGroupView statusView;
    ButtonHintsView buttonHintsView;
    UiView* content = nullptr;
};

#endif
