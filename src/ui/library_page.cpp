#include "ui/library_page.hpp"

#include <memory>

#include "downloader.h"
#include "ui/downloader_page.hpp"
#include "ui/file_browser_page.hpp"
#include "ui/page/menu_page.hpp"
#include "ui/playlists_page.hpp"
#include "ui/wifi_dialog_page.hpp"
#include "ui/ytdlp_install_page.hpp"

using namespace input;

namespace {

class LibraryPageImpl final : public MenuPageImpl<LibraryPage> {
public:
    LibraryPageImpl() : MenuPageImpl("Library", HELP_LIBRARY_MENU) {
        rows.setRows({
            {
                .id       = 0,
                .label    = "Files",
                .onSelect = [this] { getNavigator().open(FileBrowserPage{}); },
            },
            {
                .id       = 1,
                .label    = "Playlists",
                .onSelect = [this] { getNavigator().open(PlaylistsPage{}); },
            },
            {
                .id       = 2,
                .label    = "Downloader",
                .onSelect = [this] { openDownloader(); },
            },
        });
    }

    void onEnter() override { start(rows); }

private:
    /** Waits for Wi-Fi, installs the helpers where they are missing, and opens the Downloader */
    void openDownloader() {
        Downloader_init();
        getNavigator().openForResult(WifiDialogPage{}, [this](const bool& connected) {
            if (!connected) {
                Downloader_cleanup();
                return;
            }
            if (Downloader_isAvailable()) {
                getNavigator().open(DownloaderPage{});
                return;
            }
            getNavigator().openForResult(YtdlpInstallPage{}, [this](const bool& installed) {
                if (installed && Downloader_init() == 0) {
                    getNavigator().open(DownloaderPage{});
                    return;
                }
                Downloader_cleanup();
            });
        });
    }

    ArrayMenuAdapter rows;
};

}  // namespace

std::unique_ptr<UiPageImplBase> LibraryPage::createPageImpl() const {
    return std::make_unique<LibraryPageImpl>();
}
