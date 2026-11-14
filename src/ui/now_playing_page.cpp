#include "ui/now_playing_page.hpp"

#include "background.h"
#include "module_player.h"
#include "module_podcast.h"
#include "module_radio.h"
#include "ui/legacy_module_page.hpp"

namespace {

class NowPlayingPageImpl final : public LegacyModulePageImpl<NowPlayingPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override {
        switch (Background_getActive()) {
            case BG_MUSIC:   return PlayerModule_run(display, true);
            case BG_RADIO:   return RadioModule_run(display);
            case BG_PODCAST: return PodcastModule_run(display);
            default:         return MODULE_EXIT_TO_MENU;
        }
    }
};

}  // namespace

std::unique_ptr<UiPageImplBase> NowPlayingPage::createPageImpl() const {
    return std::make_unique<NowPlayingPageImpl>();
}
