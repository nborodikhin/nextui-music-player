#include "ui/view/list_screen_view.hpp"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

void layoutListScreen(UiLayoutContext& context, const ListScreenParts& parts) {
    const UiSize screen   = context.getMaxSize();
    const int padding     = SCALE1(PADDING);
    const int pillHeight  = SCALE1(PILL_SIZE);
    const SDL_Rect topRow = {0, 0, screen.w, padding + pillHeight};

    // The status group draws first and gives its width, thus the title takes the room that is
    // left and never runs under it
    UiLayoutContext statusMeasuring = context.createMeasuringContext();
    const int statusWidth           = statusMeasuring.layoutChild(parts.statusView, topRow).getSize().w;
    const SDL_Rect statusRect       = {screen.w - padding - statusWidth, padding, statusWidth, pillHeight};
    context.layoutChild(parts.statusView, statusRect);

    // The same inset on each side, thus the title and the status group keep one gap
    const int titleX          = padding + SCALE1(BUTTON_PADDING);
    const int titleWidth      = screen.w - padding - SCALE1(BUTTON_PADDING) - statusWidth - titleX;
    UiLayoutContext measuring = context.createMeasuringContext();
    const SDL_Rect titleRoom  = {0, 0, titleWidth, pillHeight};
    const int titleHeight     = measuring.layoutChild(parts.titleView, titleRoom).getSize().h;
    const int titleY          = padding + (pillHeight - titleHeight) / 2;
    context.layoutChild(parts.titleView, SDL_Rect{titleX, titleY, titleWidth, titleHeight});

    // The first row starts where the top pill row ends. The list keeps no room for the down
    // indicator, which sits under the last row, also over the button hints.
    const SDL_Rect wholeScreen    = {0, 0, screen.w, screen.h};
    UiLayoutContext upMeasuring   = context.createMeasuringContext();
    UiLayoutContext downMeasuring = context.createMeasuringContext();
    const UiSize upSize           = upMeasuring.layoutChild(parts.scrollUpView, wholeScreen).getSize();
    const UiSize downSize         = downMeasuring.layoutChild(parts.scrollDownView, wholeScreen).getSize();
    const int listY               = padding + pillHeight;
    const int listHeight          = screen.h - listY - padding - pillHeight;
    const SDL_Rect upRect         = {(screen.w - upSize.w) / 2, listY - upSize.h, upSize.w, upSize.h};
    context.layoutChild(parts.scrollUpView, upRect);
    context.layoutChild(parts.listView, SDL_Rect{padding, listY, screen.w - padding * 2, listHeight});
    context.layoutChild(parts.buttonHintsView, SDL_Rect{0, screen.h - padding - pillHeight, screen.w, pillHeight});

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
    UiView::setContext(context);
    titleView.setContext(context);
    statusView.setContext(context);
    listView.setContext(context);
    scrollUpView.setContext(context);
    scrollDownView.setContext(context);
    buttonHintsView.setContext(context);
}

void ListScreenView::layout(UiLayoutContext& context) {
    layoutListScreen(context, ListScreenParts{
                                  .titleView       = titleView,
                                  .statusView      = statusView,
                                  .listView        = listView,
                                  .scrollUpView    = scrollUpView,
                                  .scrollDownView  = scrollDownView,
                                  .buttonHintsView = buttonHintsView,
                              });
}
