#ifndef __UI_YTDLP_INSTALL_PAGE_HPP__
#define __UI_YTDLP_INSTALL_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/**
 * Runs the legacy installer of the YouTube download helpers, after the wait for Wi-Fi. Closes when
 * the installer returns.
 */
class YtdlpInstallPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
