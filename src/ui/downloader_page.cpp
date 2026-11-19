#include "ui/downloader_page.hpp"

#include "module_downloader.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class DownloaderPageImpl final : public LegacyModulePageImpl<DownloaderPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return DownloaderModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> DownloaderPage::createPageImpl() const {
    return std::make_unique<DownloaderPageImpl>();
}
