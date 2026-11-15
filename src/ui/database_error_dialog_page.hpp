#ifndef __UI_DATABASE_ERROR_DIALOG_PAGE_HPP__
#define __UI_DATABASE_ERROR_DIALOG_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/** The dialog that tells the user that the app cannot open its database, and that the app exits. */
class DatabaseErrorDialogPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
