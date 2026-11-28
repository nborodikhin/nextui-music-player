#ifndef __UI_VIEW_SCREEN_VIEW_HPP__
#define __UI_VIEW_SCREEN_VIEW_HPP__

#include <SDL2/SDL_ttf.h>

#include <cstdint>

#include "ui/view/button_hints_view.hpp"
#include "ui/view/scroll_indicator_view.hpp"
#include "ui/view/scrolling_view.hpp"
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
 * Lays out `view` in its measured size, in the middle of the width of the context and inside its
 * side margins, with its top at `y`. Pass a height in `maxHeight` to limit the height of the
 * view, or 0 for no limit. A text view with the height of its font has no space above its text.
 * Returns the height of the view.
 */
int layoutCentered(UiLayoutContext& context, UiView& view, int y, int maxHeight = 0);

/** The space that a screen keeps between its body and its content. */
enum class ContentMargins : uint8_t {
    None,
    /** The side margin at the left and at the right */
    Sides,
    /** The side margins, and one margin above the button hints */
    SidesAndFoot,
};

/**
 * A full-screen page: the title and the status group in the top pill row, the button hints in the
 * bottom pill row, and a content view in the body between them. A content that scrolls gets a
 * scroll indicator above it and one under the part that shows. It owns its views, but not the
 * content view.
 */
class ScreenView : public UiLayout {
public:
    /** The title takes the large font and the secondary color of the theme. */
    ScreenView();

    TitleTextView& getTitleView() { return titleView; }
    StatusGroupView& getStatusView() { return statusView; }
    ButtonHintsView& getButtonHintsView() { return buttonHintsView; }

    /**
     * Lays out `content` in the body, inside `margins`. The view stays with the caller. Pass null
     * for no content.
     */
    void setContent(UiView* content, ContentMargins margins = ContentMargins::None);

    /**
     * Lays out `content` as setContent() does, and shows each scroll indicator while the content
     * can scroll in its direction.
     */
    void setScrollingContent(ScrollingView* content, ContentMargins margins = ContentMargins::None);

    /** Starts the wait before the title scrolls. */
    void start();

    /** Called when a full-screen page opens over the page of this view and hides it. */
    void onPageFullyHidden();

    void layout(UiLayoutContext& context) override;

protected:
    void onAttach(UiContext& context) override;

private:
    /** Puts the title at its start, and starts the wait before it scrolls. */
    void restartTitle();
    void layoutScrollIndicators(UiLayoutContext& context, const SDL_Rect& contentRect);

    TitleTextView titleView;
    StatusGroupView statusView;
    ButtonHintsView buttonHintsView;
    UiView* content                 = nullptr;
    ScrollingView* scrollingContent = nullptr;
    ContentMargins margins          = ContentMargins::None;
    ScrollIndicatorView scrollUpView{ScrollIndicatorView::Direction::Up};
    ScrollIndicatorView scrollDownView{ScrollIndicatorView::Direction::Down};
    TaskId titleStart = 0;
    /** A start came before the attach */
    bool startPending = false;
};

#endif
