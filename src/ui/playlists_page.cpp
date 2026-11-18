#include "ui/playlists_page.hpp"

#include "module_playlist.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class PlaylistsPageImpl final : public LegacyModulePageImpl<PlaylistsPage> {
protected:
    ModuleExitReason runModule(DisplayContext* display) override { return PlaylistModule_run(display); }
};

}  // namespace

std::unique_ptr<UiPageImplBase> PlaylistsPage::createPageImpl() const {
    return std::make_unique<PlaylistsPageImpl>();
}
