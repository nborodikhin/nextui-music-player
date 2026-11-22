#ifndef __UI_CONFIRM_DIALOG_PAGE_HPP__
#define __UI_CONFIRM_DIALOG_PAGE_HPP__

#include <memory>
#include <string>

#include "ui/page/ui_page.hpp"

/**
 * A dialog that asks the user a yes or no question, with an optional message under the question.
 */
class ConfirmDialogPage final : public UiPage {
public:
    /** true: the user said yes */
    using Result = bool;

    /** Pass an empty `message` for a dialog with the question only. */
    explicit ConfirmDialogPage(std::string title, std::string message = {})
        : title(std::move(title)), message(std::move(message)) {}

    std::unique_ptr<UiPageImplBase> createPageImpl() const override;

private:
    std::string title;
    std::string message;
};

#endif
