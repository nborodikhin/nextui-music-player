#ifndef __UI_CONTROLS_HELP_PAGE_HPP__
#define __UI_CONTROLS_HELP_PAGE_HPP__

#include <memory>
#include <variant>

#include "help_screen.h"
#include "ui/ui_page.hpp"

/** The dialog that lists the controls of a screen. */
class ControlsHelpPage final : public UiPage {
public:
    using Result = std::monostate;
    explicit ControlsHelpPage(HelpId helpId) : helpId(helpId) {}
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;

    HelpId helpId;
};

#endif
