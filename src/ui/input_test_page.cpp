#ifdef DEBUG

#include "ui/input_test_page.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "input/input_state.hpp"
#include "ui/page/screen_page.hpp"
#include "ui/view/text_view.hpp"
#include "ui/view/ui_layout.hpp"
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

/** The buttons that are held, and under them the log of the events, with the newest at the bottom */
class InputLogView final : public UiLayout {
public:
    InputLogView() {
        for (int index = 0; index < kEntryCount; index++) {
            auto line = std::make_unique<TextView>(Fonts_getSmall(), true);
            line->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
            logLines.push_back(std::move(line));
        }
        heldLine.setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        setHeld(0);
    }

    /** Shows the buttons of the bits of `held`. */
    void setHeld(uint32_t held) {
        char heldText[256] = "Held:";
        for (int index = 0; index < kButtonCount; index++) {
            const Button button = static_cast<Button>(index);
            if (held & getButtonBit(button)) {
                const size_t used = strlen(heldText);
                snprintf(heldText + used, sizeof(heldText) - used, " %s", getButtonName(button));
            }
        }
        heldLine.setText(heldText);
    }

    void addEntry(uint32_t time, const std::string& stream, const std::string& type, const std::string& name);

    void layout(UiLayoutContext& context) override;

private:
    static constexpr int kEntryCount = 32;

    // A ring of the last entries. newest is the index of the newest entry
    std::array<std::string, kEntryCount> entries{};
    int newest        = -1;
    int entryCount    = 0;
    uint32_t lastTime = 0;

    TextView heldLine{Fonts_getMedium(), true};
    // One view for each line of the log. A line that does not fit on the screen is not laid out.
    std::vector<std::unique_ptr<TextView>> logLines;
};

void InputLogView::addEntry(uint32_t time, const std::string& stream, const std::string& type, const std::string& name) {
    const uint32_t delta = entryCount == 0 ? 0 : time - lastTime;
    lastTime = time;
    newest   = (newest + 1) % kEntryCount;
    if (entryCount < kEntryCount) entryCount++;
    char text[64];
    snprintf(text, sizeof(text), "%-6s  %-6s  %-12s  +%u ms", stream.c_str(), type.c_str(), name.c_str(), delta);
    entries[static_cast<size_t>(newest)] = text;
}

// Each line is a view that draws only when its text changes. The page draws no layer.
void InputLogView::layout(UiLayoutContext& context) {
    const UiSize space = context.getMaxSize();
    UiLayoutContext measuring = context.createMeasuringContext();
    const int heldHeight      = measuring.layoutChild(heldLine, SDL_Rect{0, 0, space.w, space.h}).getSize().h;
    context.layoutChild(heldLine, SDL_Rect{0, 0, space.w, heldHeight});
    // The lines of the log have one font, thus the first line gives the height of each line
    const int lineHeight = measuring.layoutChild(*logLines.front(), SDL_Rect{0, 0, space.w, space.h}).getSize().h;

    // The newest entry is at the bottom, as in a log
    const int logTop = heldHeight + SCALE1(PADDING);
    const int fit    = std::max(0, (space.h - logTop) / lineHeight);
    const int shown  = std::min({entryCount, fit, kEntryCount});
    for (int age = shown - 1, row = 0; age >= 0; age--, row++) {
        const int index = (newest - age + kEntryCount) % kEntryCount;
        TextView& line  = *logLines[static_cast<size_t>(row)];
        line.setText(entries[static_cast<size_t>(index)]);
        context.layoutChild(line, SDL_Rect{0, logTop + row * lineHeight, space.w, lineHeight});
    }
}

constexpr const char* kRightHints[] = {"SELECT+START", "BACK", nullptr};

class InputTestPageImpl final : public ScreenPageImpl<InputTestPage> {
public:
    InputTestPageImpl() : ScreenPageImpl("Input test") {
        getScreenView().getButtonHintsView().setHints(nullptr, kRightHints);
        getScreenView().setContent(&log, ContentMargins::SidesAndFoot);
    }

    void onEnter() override { getScreenView().start(); }

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

        log.setHeld(held);
        if ((held & kExitButtons) == kExitButtons) close();
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

private:
    static constexpr uint32_t kExitButtons = getButtonBit(Button::Select) | getButtonBit(Button::Start);

    void addEntry(uint32_t time, const std::string& stream, const std::string& type, const std::string& name) {
        log.addEntry(time, stream, type, name);
    }

    // The buttons that were pressed and are not released yet, from the raw events
    uint32_t held = 0;
    InputLogView log;
};

}  // namespace
}  // namespace ui

std::unique_ptr<UiPageImplBase> InputTestPage::createPageImpl() const {
    return std::make_unique<ui::InputTestPageImpl>();
}

#endif
