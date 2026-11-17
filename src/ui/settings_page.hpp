#ifndef __UI_SETTINGS_PAGE_HPP__
#define __UI_SETTINGS_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/** The settings of the app, and the way to About, the YouTube download helpers and the cache. */
class SettingsPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
