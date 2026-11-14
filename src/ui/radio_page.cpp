#include "ui/radio_page.hpp"

#include "module_radio.h"
#include "ui/legacy_module_page.hpp"

namespace {

class RadioPageImpl final : public LegacyModulePageImpl<RadioPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return RadioModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> RadioPage::createPageImpl() const {
    return std::make_unique<RadioPageImpl>();
}
