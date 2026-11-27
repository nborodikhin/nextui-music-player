#include "ui/music_playing_page.hpp"

#include "module_music_playing.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class MusicPlayingPageImpl final : public LegacyModulePageImpl<MusicPlayingPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override {
        bool queueEnded               = false;
        const ModuleExitReason reason = MusicPlayingModule_run(display, &queueEnded);
        result.queueEnded             = queueEnded;
        return reason;
    }

    MusicPlayingPage::Result getModuleResult() override { return result; }

private:
    MusicPlayingPage::Result result;
};

}  // namespace

std::unique_ptr<UiPageImplBase> MusicPlayingPage::createPageImpl() const {
    return std::make_unique<MusicPlayingPageImpl>();
}
