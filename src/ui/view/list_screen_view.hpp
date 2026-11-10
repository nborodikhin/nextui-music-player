#ifndef __UI_VIEW_LIST_SCREEN_VIEW_HPP__
#define __UI_VIEW_LIST_SCREEN_VIEW_HPP__

#include <SDL2/SDL_ttf.h>

#include "ui/view/button_hints_view.hpp"
#include "ui/view/list_view.hpp"
#include "ui/view/scroll_indicator_view.hpp"
#include "ui/view/status_group_view.hpp"
#include "ui/view/title_text_view.hpp"
#include "ui/view/ui_layout.hpp"

/** The views of a list screen. */
struct ListScreenParts {
    UiView& titleView;
    UiView& statusView;
    ListView& listView;
    UiView& scrollUpView;
    UiView& scrollDownView;
    UiView& buttonHintsView;
};

/**
 * Lays out the parts of a list screen:
 * - top of the screen - title view + status view
 * - body - list and scroll indicators
 * - bottom of the screen - button hints
 */
void layoutListScreen(UiLayoutContext& context, const ListScreenParts& parts);

/**
 * A menu screen with a list. It owns its views. The page gives the list its adapter and keeps
 * the adapter.
 */
class ListScreenView : public UiLayout {
public:
    explicit ListScreenView(TTF_Font* titleFont) : titleView(titleFont) {}

    TitleTextView& getTitleView() { return titleView; }
    StatusGroupView& getStatusView() { return statusView; }
    ListView& getListView() { return listView; }
    ButtonHintsView& getButtonHintsView() { return buttonHintsView; }

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;

private:
    TitleTextView titleView;
    StatusGroupView statusView;
    ListView listView;
    ScrollIndicatorView scrollUpView{ScrollIndicatorView::Direction::Up};
    ScrollIndicatorView scrollDownView{ScrollIndicatorView::Direction::Down};
    ButtonHintsView buttonHintsView;
};

#endif
