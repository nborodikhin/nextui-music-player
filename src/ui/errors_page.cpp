#include "ui/errors_page.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "error.h"
#include "file_utils.h"
#include "ui/view/multiline_text_view.hpp"
#include "ui/view/screen_view.hpp"
#include "ui/view/scroll_view.hpp"
#include "ui/view/spacer_view.hpp"
#include "ui/view/vertical_layout.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"

using namespace input;

namespace {

constexpr const char* kRightHints[] = {"B", "BACK", nullptr};

/** The file name of the log that launch.sh writes in the directory of the logs */
constexpr const char* kLogFileName = "music-player.txt";

/** Returns `path` for the user, with `<SDCARD>` in place of the path of the card. */
std::string shortenCardPath(const std::string& path) {
    // The result is never longer than the path with "<SDCARD>" added
    std::string shortened(path.size() + sizeof("<SDCARD>"), '\0');
    if (!get_card_display_path(path.c_str(), shortened.data(), shortened.size())) return path;
    shortened.resize(strlen(shortened.c_str()));
    return shortened;
}

std::string getIntroductionText() {
    const char* logsPath = getenv("LOGS_PATH");
    if (!logsPath || !logsPath[0]) return "See more detailed information in logs.";
    return "See more detailed information in logs: " +
           shortenCardPath(std::string(logsPath) + "/" + kLogFileName);
}

std::string getHeadingText(ErrorClass errorClass) {
    switch (errorClass) {
        case ERROR_CLASS_DATABASE:    return "Database errors:";
        case ERROR_CLASS_FILE_SYSTEM: return "File system errors:";
        case ERROR_CLASS_COUNT:       break;
    }
    return "";
}

std::unique_ptr<MultilineTextView> makeTextView(TTF_Font* font, const std::string& text) {
    auto view = std::make_unique<MultilineTextView>(font);
    view->setText(text);
    return view;
}

class ErrorsPageImpl final : public UiPageImpl<ErrorsPage> {
public:
    ErrorsPageImpl() : UiPageImpl(PageKind::FullScreen) {
        screen.getTitleView().setText("Errors");
        screen.getTitleView().setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
        screen.getButtonHintsView().setHints(nullptr, kRightHints);
        buildContent();
        scrollView.setContent(&content);
        screen.setScrollingContent(&scrollView, ContentMargins::SidesAndFoot);
    }

    std::span<const ButtonRequest> getButtonRequests() const override { return ScrollView::getButtonRequests(); }

    void layout(UiLayoutContext& context) override { context.layoutChild(screen, context.getMaxRect()); }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (scrollView.handleButton(event)) return true;
        if (event.isSingleButton(Button::B)) {
            close();
            return true;
        }
        return false;
    }

private:
    /** Fills the content from the store, as it is when the page opens. */
    void buildContent() {
        content.addChild(makeTextView(Fonts_getSmall(), getIntroductionText()));
        const int sectionSpace = Fonts_getMetric(Fonts_getMedium(), FONT_METRIC_LINE_HEIGHT) / 2;
        for (int index = 0; index < ERROR_CLASS_COUNT; index++) {
            const ErrorClass errorClass         = static_cast<ErrorClass>(index);
            const std::vector<std::string> reports = Error_getReports(errorClass);
            if (reports.empty()) continue;
            content.addChild(std::make_unique<SpacerView>(sectionSpace));
            content.addChild(makeTextView(Fonts_getMedium(), getHeadingText(errorClass)));
            for (const std::string& report : reports) {
                // A path has no words, thus each line of an item takes as many characters as fit
                auto item = makeTextView(Fonts_getSmall(), "- " + shortenCardPath(report));
                item->setWrap(MultilineTextView::Wrap::Characters);
                content.addChild(std::move(item));
            }
        }
    }

    VerticalLayout content;
    ScrollView scrollView;
    ScreenView screen{Fonts_getLarge()};
};

}  // namespace

std::unique_ptr<UiPageImplBase> ErrorsPage::createPageImpl() const {
    return std::make_unique<ErrorsPageImpl>();
}
