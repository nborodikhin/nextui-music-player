#include "ui/podcast_page.hpp"

#include "module_podcast.h"
#include "ui/legacy_module_page.hpp"

namespace {

class PodcastPageImpl final : public LegacyModulePageImpl<PodcastPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return PodcastModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> PodcastPage::createPageImpl() const {
    return std::make_unique<PodcastPageImpl>();
}
