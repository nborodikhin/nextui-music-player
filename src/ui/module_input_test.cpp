#ifdef DEBUG

#include "ui/module_input_test.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "input/input_state.hpp"
#include "ui/view/status_group_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui/view/title_text_view.hpp"
#include "ui/ui_page.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

using namespace input;

namespace ui {
namespace {

// One request of each type
constexpr std::array<ButtonRequest, 6> kRequests = {
    request::LongPress{Button::A},
    request::Repeat{Button::DpadUp},
    request::Repeat{Button::DpadDown},
    request::Repeat{Button::DpadLeft},
    request::Repeat{Button::DpadRight},
    request::Combo{Button::Select, Button::A},
};

class InputTestPage final : public UiPage {
public:
    std::span<const ButtonRequest> getButtonRequests() const override { return kRequests; }

    // Consumes each event, thus the global buttons do not act
    bool onRawEvent(const RawEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ButtonPressed>(&event.kind)) {
            held |= getButtonBit(typedEvent->button);
            addEntry(event.time, "raw", "PRESSED", getButtonName(typedEvent->button));
        }
        if (const auto* typedEvent = std::get_if<event::ButtonReleased>(&event.kind)) {
            held &= ~getButtonBit(typedEvent->button);
            addEntry(event.time, "raw", "RELEASED", getButtonName(typedEvent->button));
        }
        if (std::get_if<event::ButtonReset>(&event.kind)) {
            held = 0;
            addEntry(event.time, "raw", "RESET", "");
        }
        if (const auto* typedEvent = std::get_if<event::ButtonCancel>(&event.kind)) {
            held &= ~getButtonBit(typedEvent->button);
            addEntry(event.time, "raw", "CANCEL", getButtonName(typedEvent->button));
        }
        if (const auto* typedEvent = std::get_if<event::Media>(&event.kind)) {
            addEntry(event.time, "raw", "Media", getMediaKeyName(typedEvent->key));
        }

        if ((held & kExitButtons) == kExitButtons) {
            exitTo(MODULE_EXIT_BACK);
        }
        return true;
    }

    bool onButtonEvent(const ButtonEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) {
            addEntry(event.time, "button", "SHORT", getButtonName(typedEvent->button));
        }
        if (const auto* typedEvent = std::get_if<event::LongPress>(&event.kind)) {
            addEntry(event.time, "button", "LONG", getButtonName(typedEvent->button));
        }
        if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) {
            addEntry(event.time, "button", "REPEAT", getButtonName(typedEvent->button));
        }
        if (const auto* typedEvent = std::get_if<event::Combo>(&event.kind)) {
            std::string members = std::string(getButtonName(typedEvent->first)) + " " + getButtonName(typedEvent->second);
            addEntry(event.time, "button", "COMBO", members);
        }
        return true;
    }

    InputTestPage() {
        for (int index = 0; index < kEntryCount; index++) {
            logLines.push_back(std::make_unique<TextView>(Fonts_getSmall(), true));
        }
    }

    void setContext(UiContext& context) override;
    void layout(UiLayoutContext& context) override;

private:
    static constexpr int kEntryCount = 32;
    static constexpr uint32_t kExitButtons = getButtonBit(Button::Menu) | getButtonBit(Button::Select);

    struct Entry {
        std::string text;
    };

    void addEntry(uint32_t time, const std::string &stream, const std::string &type, const std::string &name);

    // The buttons that were pressed and are not released yet, from the raw events
    uint32_t held = 0;
    // A ring of the last entries. newest is the index of the newest entry
    std::array<Entry, kEntryCount> entries{};
    int newest     = -1;
    int entryCount = 0;
    uint32_t lastTime = 0;

    TitleTextView title{Fonts_getLarge()};
    StatusGroupView statusGroup;
    TextView heldLine{Fonts_getSmall(), true};
    TextView hintLine{Fonts_getSmall(), true};
    // One view for each line of the log. A line that does not fit on the screen is not laid out.
    std::vector<std::unique_ptr<TextView>> logLines;
};

void InputTestPage::addEntry(uint32_t time, const std::string &stream, const std::string &type, const std::string &name) {

    const uint32_t delta = entryCount == 0 ? 0 : time - lastTime;
    lastTime = time;
    newest   = (newest + 1) % kEntryCount;
    if (entryCount < kEntryCount) entryCount++;
    char text[64];
    snprintf(text, sizeof(text), "%-6s  %-6s  %-12s  +%u ms", stream.c_str(), type.c_str(),
             name.c_str(), delta);
    entries[newest].text = text;
}

void InputTestPage::setContext(UiContext& context) {
    title.setContext(context);
    statusGroup.setContext(context);
    heldLine.setContext(context);
    hintLine.setContext(context);
    for (auto& line : logLines) line->setContext(context);

    title.setText("Input test");
    title.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
    hintLine.setText("MENU + SELECT: back");
    hintLine.setColor(Theme_getColor(THEME_ROLE_SECONDARY, false));
    heldLine.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
    for (auto& line : logLines) line->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
}

// Each line is a view that draws only when its text changes. The page draws no layer.
void InputTestPage::layout(UiLayoutContext& context) {
    const UiSize screen  = context.getMaxSize();
    const int padding    = SCALE1(PADDING);
    const int pillHeight = SCALE1(PILL_SIZE);

    // The status group and the title share the top pill row, as on a list screen
    UiLayoutContext measuring = context.createMeasuringContext();
    const int statusWidth = measuring.layoutChild(statusGroup, SDL_Rect{0, 0, screen.w, padding + pillHeight}).getSize().w;
    context.layoutChild(statusGroup, SDL_Rect{screen.w - padding - statusWidth, padding, statusWidth, pillHeight});
    const int titleX      = padding + SCALE1(BUTTON_PADDING);
    const int titleWidth  = screen.w - padding - SCALE1(BUTTON_PADDING) - statusWidth - titleX;
    const int titleHeight = measuring.layoutChild(title, SDL_Rect{0, 0, titleWidth, pillHeight}).getSize().h;
    context.layoutChild(title, SDL_Rect{titleX, padding + (pillHeight - titleHeight) / 2, titleWidth, titleHeight});

    char heldText[256] = "Held:";
    for (int index = 0; index < kButtonCount; index++) {
        const Button button = static_cast<Button>(index);
        if (held & getButtonBit(button)) {
            const size_t used = strlen(heldText);
            snprintf(heldText + used, sizeof(heldText) - used, " %s", getButtonName(button));
        }
    }
    heldLine.setText(heldText);

    const int lineX      = SCALE1(PADDING * 2);
    const int lineWidth  = screen.w - lineX - padding;
    // Each line has one font, thus the held line gives the height of each line
    const int lineHeight = measuring.layoutChild(heldLine, SDL_Rect{0, 0, lineWidth, screen.h}).getSize().h;
    const int heldTop    = padding + pillHeight + padding;
    const int hintTop    = heldTop + lineHeight;
    const int logTop     = hintTop + lineHeight + padding;
    const int bottom     = screen.h - padding;
    context.layoutChild(heldLine, SDL_Rect{lineX, heldTop, lineWidth, lineHeight});
    context.layoutChild(hintLine, SDL_Rect{lineX, hintTop, lineWidth, lineHeight});

    // The newest entry is at the bottom, as in a log
    const int fit   = std::max(0, (bottom - logTop) / lineHeight);
    const int shown = std::min({entryCount, fit, kEntryCount});
    for (int age = shown - 1, row = 0; age >= 0; age--, row++) {
        const int index = (newest - age + kEntryCount) % kEntryCount;
        TextView& line  = *logLines[static_cast<size_t>(row)];
        line.setText(entries[static_cast<size_t>(index)].text.c_str());
        context.layoutChild(line, SDL_Rect{lineX, logTop + row * lineHeight, lineWidth, lineHeight});
    }
}

}  // namespace
}  // namespace ui

ModuleExitReason InputTestModule_run(DisplayContext* display) {
    ui::InputTestPage page;
    return runPageLoop(page, display);
}

#endif
