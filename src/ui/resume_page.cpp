#include "ui/resume_page.hpp"

#include "module_player.h"
#include "resume.h"
#include "ui/legacy_module_page.hpp"

namespace {

class ResumePageImpl final : public LegacyModulePageImpl<ResumePage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override {
        const ResumeState* resume = Resume_getState();
        if (!resume) return MODULE_EXIT_TO_MENU;
        return PlayerModule_runResume(display, resume);
    }
};

}  // namespace

std::unique_ptr<UiPageImplBase> ResumePage::createPageImpl() const {
    return std::make_unique<ResumePageImpl>();
}
