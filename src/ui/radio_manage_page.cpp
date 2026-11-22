#include "ui/radio_manage_page.hpp"

#include "module_radio_manage.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class RadioManagePageImpl final : public LegacyModulePageImpl<RadioManagePage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return RadioManageModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> RadioManagePage::createPageImpl() const {
    return std::make_unique<RadioManagePageImpl>();
}
