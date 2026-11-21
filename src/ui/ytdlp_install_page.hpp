#ifndef __UI_YTDLP_INSTALL_PAGE_HPP__
#define __UI_YTDLP_INSTALL_PAGE_HPP__

#include <memory>

#include "ui/page/ui_page.hpp"

/**
 * Checks the YouTube download helpers, asks the user, and installs or updates them. Open it after
 * the Wi-Fi dialog gave a connection.
 */
class YtdlpInstallPage final : public UiPage {
public:
    /** true: the helpers are installed when the page closes */
    using Result = bool;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
