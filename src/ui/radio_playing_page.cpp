#include "ui/radio_playing_page.hpp"

#include "module_radio_playing.h"
#include "radio.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class RadioPlayingPageImpl final : public LegacyModulePageImpl<RadioPlayingPage> {
public:
    explicit RadioPlayingPageImpl(std::string url) : url(std::move(url)) {}

protected:
    ModuleExitReason runModule(DisplayContext* display) override {
        char shown[RADIO_MAX_URL];
        const ModuleExitReason reason = RadioPlayingModule_run(display, url.empty() ? nullptr : url.c_str(), shown);
        shownUrl                      = shown;
        return reason;
    }

    std::string getModuleResult() override { return shownUrl; }

private:
    std::string url;
    std::string shownUrl;
};

}  // namespace

std::unique_ptr<UiPageImplBase> RadioPlayingPage::createPageImpl() const {
    return std::make_unique<RadioPlayingPageImpl>(url);
}
