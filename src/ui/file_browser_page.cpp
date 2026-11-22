#include "ui/file_browser_page.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

#include "background.h"
#include "browser.h"
#include "db.h"
#include "file_utils.h"
#include "filedb.h"
#include "music_session.h"
#include "spectrum.h"
#include "ui/add_to_playlist_page.hpp"
#include "ui/confirm_dialog_page.hpp"
#include "ui/music_playing_page.hpp"
#include "ui/page/list_screen_controller.hpp"
#include "ui/view/empty_state_view.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_row_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_theme.h"

using namespace input;

namespace {

constexpr const char* kLeftHints[]       = {"START", "CONTROLS", nullptr};
constexpr const char* kRightHints[]      = {"B", "BACK", "A", "SELECT", nullptr};
constexpr const char* kEmptyRightHints[] = {"B", "BACK", nullptr};

/** How often the page reads the database again when it changed */
constexpr uint32_t kRefreshMs = 100;

/** How many folders the way back up remembers */
constexpr size_t kHistoryDepth = 32;

/** The id of the row "..", which goes to the parent folder */
constexpr long kParentRowId = -2;
/** The id of the row Play All */
constexpr long kPlayAllRowId = -1;

bool isParentRow(const FileEntry& entry) { return entry.is_dir && std::strcmp(entry.name, "..") == 0; }

/** The rows of the folder of a BrowserContext */
class FolderAdapter final : public ListAdapter {
public:
    explicit FolderAdapter(const BrowserContext& browser) : browser(browser) {}

    int getCount() const override { return browser.entry_count; }

    long getItemId(int index) const override { return getFolderRowId(browser.entries[index]); }

    std::shared_ptr<ListItemView> getView(int index) override {
        const FileEntry& entry = browser.entries[index];
        auto row               = std::make_shared<ListRowView>(Fonts_getMedium());
        const bool hasIcons    = Icons_isLoaded();
        std::string text;
        if (entry.is_dir || entry.is_play_all) {
            text = entry.name;
            // Without icons, a text sign tells a folder and Play All from a file
            if (!hasIcons) text = (entry.is_dir ? "[" + text + "]" : "> " + text);
        } else {
            char display[sizeof(entry.name)];
            get_file_display_name(entry.name, display, sizeof(display));
            text = display;
        }
        row->setText(text);
        if (hasIcons) {
            if (entry.is_dir) {
                row->setLeftIcon([](bool selected) { return Icons_getFolder(THEME_ROLE_PRIMARY, selected); });
            } else if (entry.is_play_all) {
                row->setLeftIcon([](bool selected) { return Icons_getPlayAll(THEME_ROLE_PRIMARY, selected); });
            } else {
                const AudioFormat format = entry.format;
                row->setLeftIcon(
                    [format](bool selected) { return Icons_getForFormat(format, THEME_ROLE_PRIMARY, selected); });
            }
        }
        return row;
    }

private:
    const BrowserContext& browser;
};

/** A folder that the user went into, and the row of the cursor in the folder before it */
struct HistoryEntry {
    int dirId;
    int selected;
};

class FileBrowserPageImpl final : public UiPageImpl<FileBrowserPage>, private ListScreenListener {
public:
    FileBrowserPageImpl() : UiPageImpl(PageKind::FullScreen) {
        TitleTextView& title = screenView.getTitleView();
        // A path that changes while the title moves scrolls in after the old one
        title.setFollowText(true);
        title.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        emptyState.setMessage("No music files found");
        emptyState.setLine("Add music to /Music on your SD card");
        screenView.setEmptyState(&emptyState);
    }

    ~FileBrowserPageImpl() override { Browser_freeEntries(&browser); }

    std::span<const ButtonRequest> getButtonRequests() const override {
        return ListScreenController::getButtonRequests();
    }

    HelpId getHelpId() const override { return HELP_BROWSER; }

    void onEnter() override {
        mkdir(get_music_path(), 0755);
        screenView.getListView().setAdapter(rows);
        list.start();
        loadFolder(Db_getRootDirId(), 0);
        getScheduler().postRepeating([this] { refresh(); }, kRefreshMs);
    }

    void onExit() override {
        if (!Background_isPlaying()) Spectrum_quit();
        Browser_freeEntries(&browser);
    }

    void layout(UiLayoutContext& context) override { context.layoutChild(screenView, context.getMaxRect()); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (event.isSingleButton(Button::B)) {
            if (browser.is_root) {
                close();
            } else {
                goUp();
            }
            return true;
        }
        // The empty state hides the rows, thus no button acts on them
        if (browser.root_no_music) return false;
        if (list.onButtonEvent(event)) return true;
        const FileEntry* entry = getSelectedEntry();
        if (!entry) return false;
        if (event.isSingleButton(Button::A)) {
            openEntry(*entry);
            return true;
        }
        if (event.isSingleButton(Button::X)) {
            confirmDelete(*entry);
            return true;
        }
        if (event.isSingleButton(Button::Y)) {
            // ".." and Play All are ways to move and to play, not content to add
            if (!isParentRow(*entry) && !entry->is_play_all) {
                getNavigator().open(AddToPlaylistPage(entry->db_id, entry->is_dir));
            }
            return true;
        }
        return false;
    }

private:
    const FileEntry* getSelectedEntry() {
        const int index = screenView.getListView().getSelectedIndex();
        return index >= 0 && index < browser.entry_count ? &browser.entries[index] : nullptr;
    }

    /** Shows the folder `dirId` with the cursor on the row `selected`. */
    void loadFolder(int dirId, int selected) {
        readFolder(dirId);
        showRows(selected);
    }

    /**
     * Reads the rows of the folder `dirId`, and asks for a scan of it. The browser reads the root
     * when it cannot read the folder.
     */
    void readFolder(int dirId) {
        Browser_loadDirectory(&browser, dirId);
        FileDb_scanDir(dirId, false);
    }

    /** Shows the rows that the browser read, with the cursor on the row `selected`. */
    void showRows(int selected) {
        ListView& view = screenView.getListView();
        view.notifyDataChanged();
        if (browser.entry_count > 0) view.setSelectedIndex(selected);
        // The ids stay, thus the list keeps the selection and shows it
        list.notifyDataChanged();
        showFolder();
    }

    /** Shows the path, the empty state and the hints of the folder. */
    void showFolder() {
        screenView.getTitleView().setText("/" + std::string(browser.current_path));
        screenView.setEmptyStateForced(browser.root_no_music);
        screenView.getButtonHintsView().setHints(kLeftHints, browser.root_no_music ? kEmptyRightHints : kRightHints);
    }

    /** Reads the database again when it changed. The cursor stays on its row. */
    void refresh() {
        if (!Browser_hasUpdate(&browser)) return;
        browser.selected = screenView.getListView().getSelectedIndex();
        if (!Browser_refresh(&browser)) return;
        list.notifyDataChanged();
        showFolder();
    }

    /**
     * Goes to the parent folder. The cursor goes to the row of the history where the history
     * knows the parent, and else to the row of the folder that the user left.
     */
    void goUp() {
        const int childId  = browser.dir_id;
        const int parentId = browser.is_root ? 0 : browser.entries[0].db_id;
        int selected       = -1;
        if (!history.empty() && history.back().dirId == parentId) {
            selected = history.back().selected;
            history.pop_back();
        } else {
            history.clear();
        }
        readFolder(parentId);
        // The browser shows the root when the parent is gone, thus the history is stale
        if (browser.dir_id != parentId) {
            history.clear();
            selected = -1;
        }
        if (selected < 0) {
            selected = 0;
            for (int index = 0; index < browser.entry_count; index++) {
                if (browser.entries[index].is_dir && browser.entries[index].db_id == childId) selected = index;
            }
        }
        showRows(selected);
    }

    void openEntry(const FileEntry& entry) {
        if (isParentRow(entry)) {
            goUp();
            return;
        }
        if (entry.is_dir) {
            if (history.size() < kHistoryDepth) {
                history.push_back(HistoryEntry{browser.dir_id, screenView.getListView().getSelectedIndex()});
            }
            loadFolder(entry.db_id, 0);
            return;
        }
        if (!MusicSession_startFolder(browser.dir_id, entry.is_play_all ? 0 : entry.db_id)) return;
        getNavigator().openForResult(MusicPlayingPage{}, [this](const MusicPlayingPage::Result& result) {
            if (!result.queueEnded) return;
            history.clear();
            loadFolder(Db_getRootDirId(), 0);
        });
    }

    void confirmDelete(const FileEntry& entry) {
        if (entry.is_dir || entry.is_play_all) return;
        DbFileResult* file = Db_getFile(entry.db_id);
        if (!file) return;
        char path[512];
        const bool found = get_music_abspath(file->file.path, path, sizeof(path));
        Db_freeResult(file);
        if (!found) return;
        // The dialog keeps the path, thus a refresh of the rows under it does not change the target
        getNavigator().openForResult(ConfirmDialogPage("Delete File?", entry.name),
                                     [this, target = std::string(path)](const bool& yes) {
                                         if (!yes || unlink(target.c_str()) != 0) return;
                                         ListView& view     = screenView.getListView();
                                         const int position = view.getSelectedIndex();
                                         loadFolder(browser.dir_id, std::min(position, browser.entry_count - 1));
                                     });
    }

    BrowserContext browser{};
    FolderAdapter rows{browser};
    std::vector<HistoryEntry> history;
    ListScreenView screenView{Fonts_getLarge()};
    EmptyStateView emptyState;
    ListScreenController list{screenView, getScheduler(), *this};
};

}  // namespace

long getFolderRowId(const FileEntry& entry) {
    if (isParentRow(entry)) return kParentRowId;
    if (entry.is_play_all) return kPlayAllRowId;
    return 2L * entry.db_id + (entry.is_dir ? 0 : 1);
}

std::unique_ptr<UiPageImplBase> FileBrowserPage::createPageImpl() const {
    return std::make_unique<FileBrowserPageImpl>();
}
