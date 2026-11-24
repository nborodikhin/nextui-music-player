#ifndef __UI_FILE_BROWSER_PAGE_HPP__
#define __UI_FILE_BROWSER_PAGE_HPP__

#include <memory>
#include <variant>

#include "browser.h"
#include "ui/page/ui_page.hpp"
#include "ui/view/list_adapter.hpp"

class FileBrowserPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

/**
 * The rows of the folder of a BrowserContext. Folders and files have ids of their own in the
 * database, thus the id of a row holds the kind of the row too.
 */
class FolderAdapter final : public ListAdapter {
public:
    /** The adapter reads `browser`, which stays with the caller. */
    explicit FolderAdapter(const BrowserContext& browser) : browser(browser) {}

    /** Returns true for the row "..", which goes to the parent folder. */
    static bool isParentRow(const FileEntry& entry);

    int getCount() const override { return browser.entry_count; }
    long getItemId(int index) const override;
    std::shared_ptr<ListItemView> getView(int index) override;

private:
    const BrowserContext& browser;
};

#endif
