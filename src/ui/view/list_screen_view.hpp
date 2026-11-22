#ifndef __UI_VIEW_LIST_SCREEN_VIEW_HPP__
#define __UI_VIEW_LIST_SCREEN_VIEW_HPP__

#include <SDL2/SDL_ttf.h>

#include "ui/view/button_hints_view.hpp"
#include "ui/view/empty_state_view.hpp"
#include "ui/view/list_view.hpp"
#include "ui/view/scroll_indicator_view.hpp"
#include "ui/view/screen_view.hpp"
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
class ListScreenView : public ScreenView {
public:
    explicit ListScreenView(TTF_Font* titleFont) : ScreenView(titleFont) {}

    ListView& getListView() { return listView; }

    /**
     * Shows `emptyState` in place of the list and the scroll indicators while the list has no
     * item. The view stays with the caller. Pass null for no empty state.
     */
    void setEmptyState(EmptyStateView* emptyState) { this->emptyState = emptyState; }

    /** Pass true in `forced` to show the empty state also while the list has items. */
    void setEmptyStateForced(bool forced) { emptyStateForced = forced; }

    void layout(UiLayoutContext& context) override;

private:
    ListView listView;
    EmptyStateView* emptyState = nullptr;
    bool emptyStateForced      = false;
    ScrollIndicatorView scrollUpView{ScrollIndicatorView::Direction::Up};
    ScrollIndicatorView scrollDownView{ScrollIndicatorView::Direction::Down};
};

#endif
