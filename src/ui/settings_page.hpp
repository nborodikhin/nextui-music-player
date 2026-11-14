#ifndef __UI_SETTINGS_PAGE_HPP__
#define __UI_SETTINGS_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/ui_page.hpp"

/** Runs the legacy module of the Settings row of Main. Closes when the module returns. */
class SettingsPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
