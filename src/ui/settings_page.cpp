#include "ui/settings_page.hpp"

#include "module_settings.h"
#include "ui/legacy_module_page.hpp"

namespace {

class SettingsPageImpl final : public LegacyModulePageImpl<SettingsPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return SettingsModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> SettingsPage::createPageImpl() const {
    return std::make_unique<SettingsPageImpl>();
}
