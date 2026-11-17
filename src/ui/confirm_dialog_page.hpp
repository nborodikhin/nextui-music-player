#ifndef __UI_CONFIRM_DIALOG_PAGE_HPP__
#define __UI_CONFIRM_DIALOG_PAGE_HPP__

#include <memory>
#include <string>

#include "ui/page/ui_page.hpp"

/** A dialog that asks the user a yes or no question. A gives yes, and B gives no. */
class ConfirmDialogPage final : public UiPage {
public:
    /** true: the user said yes */
    using Result = bool;

    explicit ConfirmDialogPage(std::string title) : title(std::move(title)) {}

    std::unique_ptr<UiPageImplBase> createPageImpl() const override;

private:
    std::string title;
};

#endif
