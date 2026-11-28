#ifndef __UI_VIEW_LIST_SCREEN_VIEW_HPP__
#define __UI_VIEW_LIST_SCREEN_VIEW_HPP__

#include <SDL2/SDL_ttf.h>

#include "ui/view/empty_state_view.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_view.hpp"
#include "ui/view/screen_view.hpp"

/**
 * A menu screen with a list as its content. It owns its views. The page gives the list its adapter
 * and keeps the adapter.
 */
class ListScreenView : public ScreenView {
public:
    /** Shows the items of `adapter`, which the caller keeps alive. */
    void setAdapter(ListAdapter& adapter) { listView.setAdapter(adapter); }

    /**
     * Shows `emptyState` in place of the list and the scroll indicators while the list has no
     * item. The view stays with the caller. Pass null for no empty state.
     */
    void setEmptyState(EmptyStateView* emptyState) { this->emptyState = emptyState; }

    /** Pass true in `forced` to show the empty state also while the list has items. */
    void setEmptyStateForced(bool forced) { emptyStateForced = forced; }

    void layout(UiLayoutContext& context) override;

private:
    // The page reaches the list through the controller, which keeps the selection that it shows
    friend class ListScreenController;

    // The list is the content, thus a page does not set another one
    using ScreenView::setContent;
    using ScreenView::setScrollingContent;

    ListView listView;
    EmptyStateView* emptyState = nullptr;
    bool emptyStateForced      = false;
};

#endif
