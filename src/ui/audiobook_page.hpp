#ifndef __UI_AUDIOBOOK_PAGE_HPP__
#define __UI_AUDIOBOOK_PAGE_HPP__

#include <memory>
#include <variant>

#include "ui/page/ui_page.hpp"

/**
 * The Audiobook row of Main: the books of /Audiobook, with a Continue row for the book in progress
 * that played last. A opens the playing screen of a book.
 */
class AudiobookPage final : public UiPage {
public:
    using Result = std::monostate;
    std::unique_ptr<UiPageImplBase> createPageImpl() const override;
};

#endif
