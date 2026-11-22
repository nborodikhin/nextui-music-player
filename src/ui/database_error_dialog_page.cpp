#include "ui/database_error_dialog_page.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "ui/view/dialog_view.hpp"
#include "ui/view/multiline_text_view.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace {

/**
 * Returns the message. The log file is in LOGS_PATH, which NextUI sets, and the message gives its
 * path relative to the SD card. Without LOGS_PATH, the message names the log of the app.
 */
SpannedText getMessage() {
    SpannedText message;
    message.add("The SD card can be damaged or full.\n");
    const char* logs = getenv("LOGS_PATH");
    if (!logs || !logs[0]) return message.add("See the log of the app for the cause.");

    const size_t sdcardLength = strlen(SDCARD_PATH);
    if (strncmp(logs, SDCARD_PATH, sdcardLength) == 0 && logs[sdcardLength] == '/') logs += sdcardLength + 1;
    return message.add("See the log file for the cause:\n").add(std::string(logs) + "/music-player.txt",
                                                                  THEME_ROLE_SECONDARY);
}

class DatabaseErrorDialogPageImpl final : public UiPageImpl<DatabaseErrorDialogPage> {
public:
    DatabaseErrorDialogPageImpl()
        : UiPageImpl(PageKind::Dialog),
          dialog(SCALE1(300), DialogView::Alignment::Left, Fonts_getMedium(), Fonts_getSmall()),
          message(Fonts_getSmall()) {
        dialog.setTitle("Cannot open the database");
        message.setText(getMessage());
        dialog.setContent(&message);
        dialog.setHint("Press any button to exit");
    }

    void layout(UiLayoutContext& context) override { context.layoutChild(dialog, context.getMaxRect()); }

    bool onRawEvent(const RawEvent& event) override {
        if (isButtonPressedEvent(event)) close();
        // A media key goes on to the earphone buttons of the global handler
        return isButtonEvent(event);
    }

private:
    DialogView dialog;
    MultilineTextView message;
};

}  // namespace

std::unique_ptr<UiPageImplBase> DatabaseErrorDialogPage::createPageImpl() const {
    return std::make_unique<DatabaseErrorDialogPageImpl>();
}
