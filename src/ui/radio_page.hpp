#ifndef __UI_RADIO_PAGE_HPP__
#define __UI_RADIO_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/**
 * The station list of Online Radio. Opens on the playing screen of the radio while a station plays
 * in the background.
 */
class RadioPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
