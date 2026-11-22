#include "ui/view/multiline_text_view.hpp"

#include <algorithm>
#include <functional>

#include "ui_fonts.h"
#include "utf8.h"

namespace {

int measure(TTF_Font* font, const std::string& text) {
    int width = 0;
    if (!text.empty()) TTF_SizeUTF8(font, text.c_str(), &width, nullptr);
    return width;
}

/** Puts the words of a text on lines that are not wider than a limit, and gives each run. */
class LineBuilder {
public:
    using Emit = std::function<void(const std::string& text, ThemeRole role, int x, int line)>;

    LineBuilder(TTF_Font* font, int width, bool breakAnywhere, Emit emit)
        : font(font), width(width), breakAnywhere(breakAnywhere), emit(std::move(emit)) {}

    void addSpan(const TextSpan& span) {
        if (breakAnywhere) {
            addCharacters(span);
            return;
        }
        const std::string& text = span.text;
        size_t i                = 0;
        while (i < text.size()) {
            if (text[i] == '\n') {
                startLine(false);
                i++;
            } else if (text[i] == ' ') {
                spaces += ' ';
                i++;
            } else {
                size_t end = i;
                while (end < text.size() && text[end] != ' ' && text[end] != '\n') end++;
                addWord(text.substr(i, end - i), span.role);
                i = end;
            }
        }
    }

    /** Returns the count of lines */
    int finish() {
        flushRun();
        return line + 1;
    }

private:
    /** Puts each character after the last one, and starts a line where the next one does not fit */
    void addCharacters(const TextSpan& span) {
        const std::string& text = span.text;
        size_t i                = 0;
        while (i < text.size()) {
            if (text[i] == '\n') {
                startLine(false);
                i++;
                continue;
            }
            const size_t next          = i + std::max(1, UTF8_charBytes(&text[i]));
            const std::string character = text.substr(i, next - i);
            if (lineHasText && getEndWith(character, span.role) > width) startLine(true);
            append(character, span.role);
            i = next;
        }
    }

    void addWord(const std::string& word, ThemeRole role) {
        // The spaces at the start of a line that a wrap made do not show
        const std::string lead = lineHasText || !lineWrapped ? spaces : std::string();
        spaces.clear();
        if (getEndWith(lead + word, role) <= width) {
            append(lead + word, role);
            return;
        }
        if (lineHasText) {
            startLine(true);
            if (measure(font, word) <= width) {
                append(word, role);
                return;
            }
        }
        breakWord(word, role);
    }

    /** Puts a word that is wider than a line on lines of its own, at least one character on each */
    void breakWord(const std::string& word, ThemeRole role) {
        size_t start = 0;
        while (start < word.size()) {
            size_t end = start + std::max(1, UTF8_charBytes(&word[start]));
            while (end < word.size()) {
                const size_t next = end + std::max(1, UTF8_charBytes(&word[end]));
                if (lineEnd + measure(font, word.substr(start, next - start)) > width) break;
                end = next;
            }
            append(word.substr(start, end - start), role);
            start = end;
            if (start < word.size()) startLine(true);
        }
    }

    /** Returns the end of the line after piece in role */
    int getEndWith(const std::string& piece, ThemeRole role) const {
        if (!run.empty() && runRole == role) return runX + measure(font, run + piece);
        return lineEnd + measure(font, piece);
    }

    void append(const std::string& piece, ThemeRole role) {
        if (!run.empty() && runRole != role) flushRun();
        if (run.empty()) {
            runX    = lineEnd;
            runRole = role;
        }
        run += piece;
        lineEnd     = runX + measure(font, run);
        lineHasText = true;
    }

    void flushRun() {
        if (!run.empty()) emit(run, runRole, runX, line);
        run.clear();
    }

    /** Pass true in wrapped when the width of the line ends it, not a line break of the text */
    void startLine(bool wrapped) {
        flushRun();
        spaces.clear();
        line++;
        lineEnd     = 0;
        lineHasText = false;
        lineWrapped = wrapped;
    }

    TTF_Font* font;
    int width;
    bool breakAnywhere;
    Emit emit;
    std::string spaces;
    std::string run;
    ThemeRole runRole = THEME_ROLE_PRIMARY;
    int runX          = 0;
    int lineEnd       = 0;
    int line          = 0;
    bool lineHasText  = false;
    bool lineWrapped  = false;
};

}  // namespace

MultilineTextView::MultilineTextView(TTF_Font* font) : font(font) {}

MultilineTextView::~MultilineTextView() { freeRuns(); }

bool MultilineTextView::setWrap(Wrap newWrap) {
    if (wrapMode == newWrap) return false;
    wrapMode  = newWrap;
    wrapStale = true;
    dirty     = true;
    return true;
}

bool MultilineTextView::setText(const std::string& newText) { return setText(SpannedText().add(newText)); }

bool MultilineTextView::setText(const SpannedText& newText) {
    if (text == newText) return false;
    text      = newText;
    wrapStale = true;
    dirty     = true;
    return true;
}

void MultilineTextView::setLineHeight(int height) {
    if (height == fixedLineHeight) return;
    fixedLineHeight = height;
    dirty           = true;
}

void MultilineTextView::setMaxLines(int count) {
    if (count == maxLines) return;
    maxLines = count;
    dirty    = true;
}

void MultilineTextView::setCutToSpace(bool newCutToSpace) {
    if (newCutToSpace == cutToSpace) return;
    cutToSpace = newCutToSpace;
    dirty      = true;
}

void MultilineTextView::onAttach(UiContext&) {
    wrapStale = true;
    dirty     = true;
}

int MultilineTextView::getLineHeight() const {
    if (fixedLineHeight > 0) return fixedLineHeight;
    return Fonts_getMetric(font, FONT_METRIC_LINE_HEIGHT);
}

void MultilineTextView::freeRuns() {
    for (const Run& run : runs) {
        if (run.bitmap) SDL_FreeSurface(run.bitmap);
    }
    runs.clear();
}

void MultilineTextView::wrap(int width) {
    freeRuns();
    wrapStale    = false;
    wrappedWidth = width;
    lineCount    = 0;
    if (text.getSpans().empty()) return;

    // The text is white, thus the color mod of the draw gives it the color of its role
    //noinspection HardcodedColor
    const SDL_Color white = {255, 255, 255, 255};
    LineBuilder builder(font, width, wrapMode == Wrap::Characters, [this, white](const std::string& part, ThemeRole role, int x, int line) {
        runs.push_back(Run{TTF_RenderUTF8_Blended(font, part.c_str(), white), role, x, line});
    });
    for (const TextSpan& span : text.getSpans()) builder.addSpan(span);
    lineCount = builder.finish();
}

void MultilineTextView::layout(UiLayoutContext& context) {
    const int width = context.getMaxSize().w;
    if (wrapStale || width != wrappedWidth) {
        wrap(width);
        dirty = true;
    }
    const int lineHeight = getLineHeight();
    int shown            = lineCount;
    if (maxLines > 0) shown = std::min(shown, maxLines);
    if (cutToSpace) shown = std::min(shown, context.getMaxSize().h / lineHeight);
    if (shown != shownLineCount) {
        shownLineCount = shown;
        dirty          = true;
    }
    context.setSize(UiSize{width, shownLineCount * lineHeight});
}

void MultilineTextView::draw(UiDrawContext& context) {
    dirty               = false;
    SDL_Surface* target = context.getSurface();
    if (!target) return;
    const int lineHeight = getLineHeight();
    const int textTop    = (lineHeight - TTF_FontHeight(font)) / 2;
    for (const Run& run : runs) {
        if (!run.bitmap || run.line >= shownLineCount) continue;
        const SDL_Color color = Theme_getColor(run.role, false);
        SDL_SetSurfaceBlendMode(run.bitmap, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceColorMod(run.bitmap, color.r, color.g, color.b);
        SDL_Rect at = {run.x, run.line * lineHeight + textTop, 0, 0};
        SDL_BlitSurface(run.bitmap, nullptr, target, &at);
    }
}
