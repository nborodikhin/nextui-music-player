#ifndef __UI_AUDIOBOOK_CHAPTERS_PAGE_HPP__
#define __UI_AUDIOBOOK_CHAPTERS_PAGE_HPP__

#include <memory>
#include <optional>

#include "ui/page/ui_page.hpp"

/**
 * The chapters of the book of the audiobook session, with the cursor on the chapter that plays.
 * Gives the chapter that A picks, or no chapter after B.
 */
class AudiobookChaptersPage final : public UiPage {
public:
    using Result = std::optional<int>;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
