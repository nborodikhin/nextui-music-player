#include "ui/add_to_playlist_page.hpp"

#include "add_to_playlist.h"
#include "help_screen.h"
#include "module_common.h"
#include "ui/page/legacy_module_page.hpp"

namespace {

class AddToPlaylistPageImpl final : public LegacyModulePageImpl<AddToPlaylistPage> {
public:
    AddToPlaylistPageImpl(int id, bool isFolder) : id(id), isFolder(isFolder) {}

protected:
    ModuleExitReason runModule(DisplayContext* display) override {
        if (isFolder) {
            AddToPlaylist_openDir(id);
        } else {
            AddToPlaylist_open(id);
        }
        int showSetting = 0;
        while (AddToPlaylist_isActive()) {
            ModuleCommon_frameBegin(true);
            // The frame start can make a new display, thus the surface comes again
            SDL_Surface* const screen = DisplayHelper_getSurface(display);

            // The dialog covers the screen, thus START shows no help over it
            const GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &showSetting, HELP_NONE);
            if (global.should_quit) return MODULE_EXIT_QUIT;
            if (global.input_consumed) {
                ModuleCommon_frameEnd(screen);
                continue;
            }

            if (AddToPlaylist_handleInput()) break;
            AddToPlaylist_render(screen);
            ModuleCommon_markSurfaceDrawn();
            ModuleCommon_frameEnd(screen);
        }
        return MODULE_EXIT_BACK;
    }

private:
    int id;
    bool isFolder;
};

}  // namespace

std::unique_ptr<UiPageImplBase> AddToPlaylistPage::createPageImpl() const {
    return std::make_unique<AddToPlaylistPageImpl>(id, isFolder);
}
