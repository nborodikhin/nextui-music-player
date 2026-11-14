#include "ui/quit_dialog_page.hpp"

#include "ui/view/dialog_view.hpp"
#include "ui_fonts.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

class QuitDialogPageImpl final : public UiPageImpl<QuitDialogPage> {
public:
    QuitDialogPageImpl()
        : UiPageImpl(PageKind::Dialog),
          dialog(SCALE1(280), DialogView::Alignment::Center, Fonts_getMedium(), Fonts_getSmall()) {
        dialog.setTitle("Quit Music Player?");
        dialog.setHint("A: Yes   B: No");
    }

    void setContext(UiContext& context) override { dialog.setContext(context); }
    void layout(UiLayoutContext& context) override { context.layoutChild(dialog, context.getMaxRect()); }

    bool onRawEvent(const RawEvent& event) override {
        // A media key goes on to the earphone buttons of the global handler
        return isButtonEvent(event);
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        // START with a volume button is a combo, thus the volume changes and the dialog stays
        if (event.isSingleButton(Button::A)) finish(true);
        if (event.isSingleButton(Button::B) || event.isSingleButton(Button::Start)) close();
        return true;
    }

private:
    DialogView dialog;
};

}  // namespace

std::unique_ptr<UiPageImplBase> QuitDialogPage::createPageImpl() const {
    return std::make_unique<QuitDialogPageImpl>();
}
