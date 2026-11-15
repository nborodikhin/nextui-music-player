#ifndef __UI_VIEW_SPANNED_TEXT_HPP__
#define __UI_VIEW_SPANNED_TEXT_HPP__

#include <string>
#include <utility>
#include <vector>

#include "ui_theme.h"

/** A part of a text that shows in the color of one theme role. */
struct TextSpan {
    std::string text;
    ThemeRole role = THEME_ROLE_PRIMARY;

    bool operator==(const TextSpan&) const = default;
};

/**
 * A text made of spans, in their order. A line break in a span starts a new line. A span starts
 * and ends between words: a line can break between two spans.
 */
class SpannedText {
public:
    /** Adds text in the color of role at the end. Returns: this text, for the next call */
    SpannedText& add(std::string text, ThemeRole role = THEME_ROLE_PRIMARY) {
        spans.push_back(TextSpan{std::move(text), role});
        return *this;
    }

    const std::vector<TextSpan>& getSpans() const { return spans; }

    bool operator==(const SpannedText&) const = default;

private:
    std::vector<TextSpan> spans;
};

#endif
