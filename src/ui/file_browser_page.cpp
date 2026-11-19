#include "ui/file_browser_page.hpp"

#include "module_file_browser.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class FileBrowserPageImpl final : public LegacyModulePageImpl<FileBrowserPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return FileBrowserModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> FileBrowserPage::createPageImpl() const {
    return std::make_unique<FileBrowserPageImpl>();
}
