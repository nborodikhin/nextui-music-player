#include "ui/file_browser_page.hpp"

#include <cstring>
#include <string>

#include "file_utils.h"
#include "ui/view/list_row_view.hpp"
#include "ui_fonts.h"
#include "ui_icons.h"

namespace {

/** The id of the row "..", which goes to the parent folder */
constexpr long kParentRowId = -2;
/** The id of the row Play All */
constexpr long kPlayAllRowId = -1;

}  // namespace

bool FolderAdapter::isParentRow(const FileEntry& entry) { return entry.is_dir && std::strcmp(entry.name, "..") == 0; }

long FolderAdapter::getItemId(int index) const {
    const FileEntry& entry = browser.entries[index];
    if (isParentRow(entry)) return kParentRowId;
    if (entry.is_play_all) return kPlayAllRowId;
    return 2L * entry.db_id + (entry.is_dir ? 0 : 1);
}

std::shared_ptr<ListItemView> FolderAdapter::getView(int index) {
    const FileEntry& entry = browser.entries[index];
    auto row               = std::make_shared<ListRowView>();
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
            row->setLeftIcon(ICON_FOLDER);
        } else if (entry.is_play_all) {
            row->setLeftIcon(ICON_PLAY_ALL);
        } else {
            row->setLeftIcon(Icons_getFormatIcon(entry.format));
        }
    }
    return row;
}
