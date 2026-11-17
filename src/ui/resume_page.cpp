#include "ui/resume_page.hpp"

#include "module_music_playing.h"
#include "music_session.h"
#include "resume.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class ResumePageImpl final : public LegacyModulePageImpl<ResumePage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override {
        const ResumeState* resume = Resume_getState();
        if (!resume || !MusicSession_startResume(resume)) return MODULE_EXIT_TO_MENU;
        return MusicPlayingModule_run(display, nullptr);
    }
};

}  // namespace

std::unique_ptr<UiPageImplBase> ResumePage::createPageImpl() const {
    return std::make_unique<ResumePageImpl>();
}
