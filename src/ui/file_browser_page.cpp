#include "ui/file_browser_page.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "browser.h"
#include "db.h"
#include "file_utils.h"
#include "filedb.h"
#include "music_session.h"
#include "ui/add_to_playlist_page.hpp"
#include "ui/confirm_dialog_page.hpp"
#include "ui/music_playing_page.hpp"
#include "ui/page/list_screen_controller.hpp"
#include "ui/view/empty_state_view.hpp"
#include "ui/view/list_screen_view.hpp"
#include "ui_fonts.h"
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
        ensure_music_dir();
        screenView.setAdapter(rows);
        list.start();
        loadFolder(Db_getRootDirId(), 0);
        getScheduler().postRepeating([this] { refresh(); }, kRefreshMs);
    }

    void onExit() override { Browser_freeEntries(&browser); }

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
            if (!FolderAdapter::isParentRow(*entry) && !entry->is_play_all) {
                getNavigator().open(AddToPlaylistPage(entry->db_id, entry->is_dir));
            }
            return true;
        }
        return false;
    }

private:
    const FileEntry* getSelectedEntry() {
        const int index = list.getSelectedIndex();
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

    /**
     * Shows the rows that the browser read, with the cursor on the row `selected`. Pass true in
     * `center` to put that row in the middle of the list.
     */
    void showRows(int selected, bool center = false) {
        list.notifyDataChanged();
        if (browser.entry_count > 0) list.setSelectedIndex(selected, center);
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
        browser.selected = list.getSelectedIndex();
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
        // The row of the folder goes to the middle of the list, thus the rows around it show
        showRows(selected, true);
    }

    void openEntry(const FileEntry& entry) {
        if (FolderAdapter::isParentRow(entry)) {
            goUp();
            return;
        }
        if (entry.is_dir) {
            if (history.size() < kHistoryDepth) {
                history.push_back(HistoryEntry{browser.dir_id, list.getSelectedIndex()});
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
        DbFileResult* file_result = Db_getFile(entry.db_id);
        char path[512];
        const bool found =  file_result && get_music_abspath(file_result->file.path, path, sizeof(path));
        Db_freeResult(file_result);
        if (!found) return;

        // The dialog keeps the path, thus a refresh of the rows under it does not change the target
        getNavigator().openForResult(ConfirmDialogPage("Delete File?", entry.name),
                                     [this, target = std::string(path)](const bool& yes) {
                                         if (yes) onDeleteConfirmed(target);
                                     });
    }

    /** Deletes the file at `path`, and shows the folder again with the cursor at its place. */
    void onDeleteConfirmed(const std::string& path) {
        if (unlink(path.c_str()) != 0) return;
        loadFolder(browser.dir_id, std::min(list.getSelectedIndex(), browser.entry_count - 1));
    }

    BrowserContext browser{};
    FolderAdapter rows{browser};
    std::vector<HistoryEntry> history;
    ListScreenView screenView{Fonts_getLarge()};
    EmptyStateView emptyState;
    ListScreenController list{screenView, getScheduler(), *this};
};

}  // namespace

std::unique_ptr<UiPageImplBase> FileBrowserPage::createPageImpl() const {
    return std::make_unique<FileBrowserPageImpl>();
}
