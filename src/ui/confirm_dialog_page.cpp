#include "ui/confirm_dialog_page.hpp"

#include "ui/view/dialog_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

class ConfirmDialogPageImpl final : public UiPageImpl<ConfirmDialogPage> {
public:
    ConfirmDialogPageImpl(const std::string& title, const std::string& message)
        : UiPageImpl(PageKind::Dialog),
          dialog(SCALE1(280), DialogView::Alignment::Center, Fonts_getMedium(), Fonts_getSmall()),
          messageView(Fonts_getSmall(), true) {
        dialog.setTitle(title);
        if (!message.empty()) {
            messageView.setText(message);
            messageView.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
            dialog.setContent(&messageView);
        }
        dialog.setHint("A: Yes   B: No");
    }

    void layout(UiLayoutContext& context) override { context.layoutChild(dialog, context.getMaxRect()); }

    bool onRawEvent(const RawEvent& event) override {
        // A media key goes on to the earphone buttons of the global handler
        return isButtonEvent(event);
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (event.isSingleButton(Button::A)) finish(true);
        if (event.isSingleButton(Button::B)) close();
        return true;
    }

private:
    DialogView dialog;
    TextView messageView;
};

}  // namespace

std::unique_ptr<UiPageImplBase> ConfirmDialogPage::createPageImpl() const {
    return std::make_unique<ConfirmDialogPageImpl>(title, message);
}
