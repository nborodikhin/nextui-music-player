#ifndef __UI_APP_UPDATE_PAGE_HPP__
#define __UI_APP_UPDATE_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/**
 * Downloads and installs the update that the last check found, and shows its release notes and
 * its progress. The automatic sleep is off while the page is open.
 */
class AppUpdatePage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
