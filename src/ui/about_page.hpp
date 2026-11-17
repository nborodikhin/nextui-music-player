#ifndef __UI_ABOUT_PAGE_HPP__
#define __UI_ABOUT_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/** Runs the legacy module of About and the app update. Closes when the module returns. */
class AboutPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
