#include "ui/about_page.hpp"

#include "module_about.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class AboutPageImpl final : public LegacyModulePageImpl<AboutPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return AboutModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> AboutPage::createPageImpl() const {
    return std::make_unique<AboutPageImpl>();
}
