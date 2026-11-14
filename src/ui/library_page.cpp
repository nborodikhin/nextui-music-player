#include "ui/library_page.hpp"

#include "module_library.h"
#include "ui/legacy_module_page.hpp"

namespace {

class LibraryPageImpl final : public LegacyModulePageImpl<LibraryPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return LibraryModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> LibraryPage::createPageImpl() const {
    return std::make_unique<LibraryPageImpl>();
}
