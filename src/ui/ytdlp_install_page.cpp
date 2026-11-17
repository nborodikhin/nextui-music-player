#include "ui/ytdlp_install_page.hpp"

#include "downloader.h"
#include "module_downloader.h"
#include "ui/page/legacy_module_page.hpp"
#include "wifi.h"

namespace {

class YtdlpInstallPageImpl final : public LegacyModulePageImpl<YtdlpInstallPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override {
        int showSetting = 0;
        // The installer also installs: the helpers ship apart from the app
        Downloader_init();
        if (!Wifi_ensureConnected(DisplayHelper_getSurface(display), showSetting)) return MODULE_EXIT_TO_MENU;
        if (DownloaderModule_runInstall(display, &showSetting) == YTDLP_INSTALL_QUIT) return MODULE_EXIT_QUIT;
        return MODULE_EXIT_TO_MENU;
    }
};

}  // namespace

std::unique_ptr<UiPageImplBase> YtdlpInstallPage::createPageImpl() const {
    return std::make_unique<YtdlpInstallPageImpl>();
}
