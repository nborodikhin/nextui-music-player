#ifndef __UI_ABOUT_PAGE_HPP__
#define __UI_ABOUT_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/** The name and the version of the app, the state of the update check, and a QR code of the project. */
class AboutPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
