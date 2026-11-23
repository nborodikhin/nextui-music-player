#include "ui/view/list_screen_view.hpp"

void ListScreenView::layout(UiLayoutContext& context) {
    if (emptyState && listView.getItemCount() == 0) {
        setContent(emptyState, ContentMargins::Sides);
    } else {
        setScrollingContent(&listView, ContentMargins::Sides);
    }
    ScreenView::layout(context);
}
