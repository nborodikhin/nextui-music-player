#include "ui/view/list_screen_view.hpp"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

void layoutListScreen(UiLayoutContext& context, const ListScreenParts& parts) {
    const UiSize screen = context.getMaxSize();
    const SDL_Rect body = layoutScreenFrame(context, parts.titleView, parts.statusView, parts.buttonHintsView);

    // The list keeps no room for the down indicator, which sits under the last row, also over the
    // button hints.
    const SDL_Rect wholeScreen    = {0, 0, screen.w, screen.h};
    UiLayoutContext upMeasuring   = context.createMeasuringContext();
    UiLayoutContext downMeasuring = context.createMeasuringContext();
    const UiSize upSize           = upMeasuring.layoutChild(parts.scrollUpView, wholeScreen).getSize();
    const UiSize downSize         = downMeasuring.layoutChild(parts.scrollDownView, wholeScreen).getSize();
    const int listY               = body.y;
    const SDL_Rect upRect         = {(screen.w - upSize.w) / 2, listY - upSize.h, upSize.w, upSize.h};
    context.layoutChild(parts.scrollUpView, upRect);
    context.layoutChild(parts.listView, body);

    const int first = parts.listView.getFirstVisibleIndex();
    const int last  = parts.listView.getLastVisibleIndex();
    int listBottom  = listY;
    for (int index = first; index >= 0 && index <= last; index++) {
        listBottom += parts.listView.getItemView(index).getSize().h;
    }
    const SDL_Rect downRect = {(screen.w - downSize.w) / 2, listBottom, downSize.w, downSize.h};
    context.layoutChild(parts.scrollDownView, downRect);

    // The compositor reads the visibility after the layout
    parts.scrollUpView.setVisible(first > 0);
    parts.scrollDownView.setVisible(last >= 0 && last < parts.listView.getItemCount() - 1);
}

void ListScreenView::setContext(UiContext& context) {
    ScreenView::setContext(context);
    listView.setContext(context);
    scrollUpView.setContext(context);
    scrollDownView.setContext(context);
}

void ListScreenView::layout(UiLayoutContext& context) {
    // The scroll indicators sit over the pill rows, thus the list screen lays out the whole screen
    layoutListScreen(context, ListScreenParts{
                                  .titleView       = getTitleView(),
                                  .statusView      = getStatusView(),
                                  .listView        = listView,
                                  .scrollUpView    = scrollUpView,
                                  .scrollDownView  = scrollDownView,
                                  .buttonHintsView = getButtonHintsView(),
                              });
}
