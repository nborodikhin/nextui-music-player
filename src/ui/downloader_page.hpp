#ifndef __UI_DOWNLOADER_PAGE_HPP__
#define __UI_DOWNLOADER_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

class DownloaderPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
