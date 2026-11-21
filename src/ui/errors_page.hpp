#ifndef __UI_ERRORS_PAGE_HPP__
#define __UI_ERRORS_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/** Shows the errors that the app reported in this run. */
class ErrorsPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
