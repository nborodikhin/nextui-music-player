#ifndef __UI_QUIT_DIALOG_PAGE_HPP__
#define __UI_QUIT_DIALOG_PAGE_HPP__

#include <memory>

#include "ui/ui_page.hpp"

/** The dialog that asks before the app quits. */
class QuitDialogPage final : public UiPage {
public:
    /** true: the user confirmed the quit */
    using Result = bool;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
