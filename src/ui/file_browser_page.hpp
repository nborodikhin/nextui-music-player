#ifndef __UI_FILE_BROWSER_PAGE_HPP__
#define __UI_FILE_BROWSER_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

class FileBrowserPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
