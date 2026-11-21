#ifndef __UI_WIFI_DIALOG_PAGE_HPP__
#define __UI_WIFI_DIALOG_PAGE_HPP__

#include <memory>

#include "ui/page/ui_page.hpp"

/**
 * Waits for Wi-Fi, or closes with false. Closes at once with true when Wi-Fi is connected. A
 * failed wait shows a toast.
 */
class WifiDialogPage final : public UiPage {
public:
    /** true: the device is connected */
    using Result = bool;

    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
