#ifndef __UI_INPUT_TEST_PAGE_HPP__
#define __UI_INPUT_TEST_PAGE_HPP__

#ifdef DEBUG

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/**
 * The input test of the debug kind: it shows the held buttons and the last raw events and button
 * events. It takes each event, thus the global buttons do not act. MENU + SELECT closes it.
 */
class InputTestPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif

#endif
