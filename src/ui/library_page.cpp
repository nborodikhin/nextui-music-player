#include "ui/library_page.hpp"

#include <iterator>
#include <memory>

#include "downloader.h"
#include "ui/downloader_page.hpp"
#include "ui/file_browser_page.hpp"
#include "ui/page/list_screen_controller.hpp"
#include "ui/playlists_page.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui/wifi_dialog_page.hpp"
#include "ui/ytdlp_install_page.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"

using namespace input;

namespace {

/** An item of Library */
enum class LibraryItem : int8_t {
    Files,
    Playlists,
    Downloader,
};

struct LibraryRow {
    LibraryItem item;
    const char* label;
};

constexpr LibraryRow kRows[] = {
    {LibraryItem::Files,      "Files"     },
    {LibraryItem::Playlists,  "Playlists" },
    {LibraryItem::Downloader, "Downloader"},
};

constexpr int kRowCount = static_cast<int>(std::size(kRows));

constexpr const char* kLeftHints[]  = {"START", "CONTROLS", nullptr};
constexpr const char* kRightHints[] = {"B", "BACK", "A", "OPEN", nullptr};

/** The rows of Library */
class LibraryListAdapter final : public ListAdapter {
public:
    int getCount() const override { return kRowCount; }
    long getItemId(int index) const override { return index; }
    std::shared_ptr<ListItemView> getView(int index) override {
        auto row = std::make_shared<ListRowView>(Fonts_getMedium());
        row->setText(kRows[index].label);
        return row;
    }
};

class LibraryPageImpl final : public UiPageImpl<LibraryPage>, private ListScreenListener {
public:
    LibraryPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screenView.getTitleView().setText("Library");
        screenView.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screenView.getButtonHintsView().setHints(kLeftHints, kRightHints);
    }

    std::span<const ButtonRequest> getButtonRequests() const override {
        return ListScreenController::getButtonRequests();
    }

    HelpId getHelpId() const override { return HELP_LIBRARY_MENU; }

    void onEnter() override {
        screenView.setAdapter(rows);
        list.start();
    }

    void layout(UiLayoutContext& context) override { context.layoutChild(screenView, context.getMaxRect()); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (list.onButtonEvent(event)) return true;
        if (event.isSingleButton(Button::A)) {
            openItem(list.getSelectedIndex());
            return true;
        }
        if (event.isSingleButton(Button::B)) {
            close();
            return true;
        }
        return false;
    }

private:
    void openItem(int index) {
        if (index < 0 || index >= kRowCount) return;
        switch (kRows[index].item) {
            case LibraryItem::Files:      getNavigator().open(FileBrowserPage{}); break;
            case LibraryItem::Playlists:  getNavigator().open(PlaylistsPage{}); break;
            case LibraryItem::Downloader: openDownloader(); break;
        }
    }

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

    LibraryListAdapter rows;
    ListScreenView screenView{Fonts_getLarge()};
    ListScreenController list{screenView, getScheduler(), *this};
};

}  // namespace

std::unique_ptr<UiPageImplBase> LibraryPage::createPageImpl() const {
    return std::make_unique<LibraryPageImpl>();
}
