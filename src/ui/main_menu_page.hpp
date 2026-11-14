#ifndef __UI_MAIN_MENU_PAGE_HPP__
#define __UI_MAIN_MENU_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/ui_page.hpp"

/** The main menu: the first page of the app. Its rows open the other screens. */
class MainMenuPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
