#ifndef __UI_FILE_BROWSER_PAGE_HPP__
#define __UI_FILE_BROWSER_PAGE_HPP__

#include <memory>
#include <variant>

#include "browser.h"
#include "ui/page/ui_page.hpp"

/**
 * Returns the id of the row of `entry` in the list of the file browser. Folders and files have ids
 * of their own in the database, thus the id holds the kind of the row too.
 */
long getFolderRowId(const FileEntry& entry);

class FileBrowserPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
