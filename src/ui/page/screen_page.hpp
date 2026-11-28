#ifndef __UI_PAGE_SCREEN_PAGE_HPP__
#define __UI_PAGE_SCREEN_PAGE_HPP__

#include <string>

#include "ui/page/ui_page.hpp"
#include "ui/view/screen_view.hpp"
#include "ui/view/ui_layout_context.hpp"

/**
 * The base of a full-screen page: the title, the status group, the button hints and a content.
 * Its title starts again when a full-screen page over it closes.
 */
template <class Page, class View = ScreenView>
class ScreenPageImpl : public UiPageImpl<Page> {
protected:
    explicit ScreenPageImpl(const std::string& title) : UiPageImpl<Page>(PageKind::FullScreen) {
        screenView.getTitleView().setText(title);
    }

    View& getScreenView() { return screenView; }

    void layout(UiLayoutContext& context) override { context.layoutChild(screenView, context.getMaxRect()); }

    void onFullyHidden() override { screenView.onPageFullyHidden(); }

private:
    View screenView;
};

#endif
