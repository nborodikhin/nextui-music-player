#include "ui/controls_help_page.hpp"

#include <memory>
#include <vector>

#include "ui/view/dialog_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui_fonts.h"
#include "ui_main.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

/** The rows of a controls help: the button on the left, and its action on the right. */
class ControlsHelpView : public UiView {
public:
    /** Shows the rows of controls, which ends with a row of NULLs, in font. */
    ControlsHelpView(const ControlHelp* controls, TTF_Font* font);

    /** Returns the height of all rows, in pixels. */
    int getHeight() const;

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;

private:
    struct Row {
        std::unique_ptr<TextView> button;
        std::unique_ptr<TextView> action;
    };

    std::vector<Row> rows;
};

int getRowHeight() { return SCALE1(18); }

/** The width of the column of the buttons */
int getButtonWidth() { return SCALE1(75); }

ControlsHelpView::ControlsHelpView(const ControlHelp* controls, TTF_Font* font) {
    for (const ControlHelp* control = controls; control->button; control++) {
        Row row{
            .button = std::make_unique<TextView>(font, true),
            .action = std::make_unique<TextView>(font, true),
        };
        row.button->setText(control->button);
        row.button->setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        row.action->setText(control->action);
        row.action->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        rows.push_back(std::move(row));
    }
}

int ControlsHelpView::getHeight() const {
    return static_cast<int>(rows.size()) * getRowHeight();
}

void ControlsHelpView::setContext(UiContext& context) {
    UiView::setContext(context);
    for (Row& row : rows) {
        row.button->setContext(context);
        row.action->setContext(context);
    }
}

void ControlsHelpView::layout(UiLayoutContext& context) {
    const int width = context.getMaxSize().w;
    int y           = 0;
    for (Row& row : rows) {
        context.layoutChild(*row.button, SDL_Rect{0, y, getButtonWidth(), getRowHeight()});
        context.layoutChild(*row.action, SDL_Rect{getButtonWidth(), y, width - getButtonWidth(), getRowHeight()});
        y += getRowHeight();
    }
}

/**
 * The buttons that close Controls Help when pressed. START closes it on a short press or a long
 * press, thus START with a volume button does not close it.
 */
constexpr uint32_t kHelpCloseButtons =
    getButtonBit(Button::A) | getButtonBit(Button::B) | getButtonBit(Button::X) | getButtonBit(Button::Y) |
    getButtonBit(Button::Select) | getButtonBit(Button::DpadUp) |
    getButtonBit(Button::DpadDown) | getButtonBit(Button::DpadLeft) | getButtonBit(Button::DpadRight) |
    getButtonBit(Button::L1) | getButtonBit(Button::R1) | getButtonBit(Button::Menu);

class ControlsHelpPageImpl final : public UiPageImpl<ControlsHelpPage> {
public:
    explicit ControlsHelpPageImpl(HelpId helpId)
        : UiPageImpl(PageKind::Dialog),
          dialog(SCALE1(240), DialogView::Alignment::Left, Fonts_getMedium(), Fonts_getSmall()),
          rows(get_controls_help(helpId, &title), Fonts_getSmall()) {
        dialog.setTitle(title);
        dialog.setContent(&rows, rows.getHeight());
        dialog.setHint("Press any button to close");
    }

    void setContext(UiContext& context) override { dialog.setContext(context); }
    void layout(UiLayoutContext& context) override { context.layoutChild(dialog, context.getMaxRect()); }

    bool onRawEvent(const RawEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ButtonPressed>(&event.kind)) {
            if (getButtonBit(typedEvent->button) & kHelpCloseButtons) close();
        }
        // A media key goes on to the earphone buttons of the global handler
        return isButtonEvent(event);
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (event.isSingleButton(Button::Start)) close();
        return true;
    }

private:
    const char* title = nullptr;
    DialogView dialog;
    ControlsHelpView rows;
};

}  // namespace

std::unique_ptr<UiPageImplBase> ControlsHelpPage::createPageImpl() const {
    return std::make_unique<ControlsHelpPageImpl>(helpId);
}
