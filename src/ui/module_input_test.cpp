#ifdef DEBUG

#include "ui/module_input_test.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "input/input_state.hpp"
#include "ui_page.hpp"
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

    void onRawEvent(const RawEvent& event) override {
        if (const auto* typedEvent = std::get_if<event::ButtonDown>(&event.kind)) {
            held |= getButtonBit(typedEvent->button);
            addEntry(event.time, "raw", "DOWN", getButtonName(typedEvent->button));
        }
        if (const auto* typedEvent = std::get_if<event::ButtonUp>(&event.kind)) {
            held &= ~getButtonBit(typedEvent->button);
            addEntry(event.time, "raw", "UP", getButtonName(typedEvent->button));
        }
        if (const auto* typedEvent = std::get_if<event::ButtonReset>(&event.kind)) {
            held = 0;
            addEntry(event.time, "raw", "RESET", "");
        }
        if (const auto* typedEvent = std::get_if<event::Media>(&event.kind)) {
            addEntry(event.time, "raw", "Media", getMediaKeyName(typedEvent->key));
        }

        if ((held & kExitButtons) == kExitButtons) {
            exitTo(MODULE_EXIT_BACK);
        }
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

    void draw(SDL_Surface* screen, bool useLayers) override;

protected:
    void onInvalidateAll() override {
        drawnStatusGroupWidth.reset();
        drawnHeld.reset();
        drawnLogVersion.reset();
    }

private:
    static constexpr int kEntryCount = 32;
    static constexpr uint32_t kExitButtons = getButtonBit(Button::Menu) | getButtonBit(Button::Select);

    struct Entry {
        std::string text;
    };

    void addEntry(uint32_t time, const std::string &stream, const std::string &type, const std::string &name);

    // The buttons that went down and are not up yet, from the raw events
    uint32_t held = 0;
    // A ring of the last entries. newest is the index of the newest entry
    std::array<Entry, kEntryCount> entries{};
    int newest     = -1;
    int entryCount = 0;
    uint32_t lastTime = 0;
    // Goes up by one for each entry, thus a draw finds a change of the log
    uint32_t logVersion = 0;

    // The state that the screen shows. An empty value matches no state, thus the next draw
    // paints that part.
    std::optional<int> drawnStatusGroupWidth;
    std::optional<uint32_t> drawnHeld;
    std::optional<uint32_t> drawnLogVersion;
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
    logVersion++;
    invalidate();
}

// Draws one line of text at the left edge. Returns the height of the line.
int drawTextLine(SDL_Surface* screen, TTF_Font* font, const char* text, SDL_Color color, int y) {
    SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, text, color);
    if (!rendered) return TTF_FontLineSkip(font);
    SDL_Rect at = {
        .x = SCALE1(PADDING * 2),
        .y = y,
        .w = 0,
        .h = 0,
    };
    SDL_BlitSurface(rendered, nullptr, screen, &at);
    SDL_FreeSurface(rendered);
    return TTF_FontLineSkip(font);
}

// Fills the rows from top to bottom with the clear color of the screen.
void clearRows(SDL_Surface* screen, int top, int bottom) {
    SDL_Rect rows = {
        .x = 0,
        .y = top,
        .w = screen->w,
        .h = bottom - top,
    };
    // Transparent black is the clear color of the platform, as GFX_clear() uses it
    SDL_FillRect(screen, &rows, 0);
}

// The page draws no layer of its own, thus useLayers changes nothing. Each part draws only
// when its state differs from the state that the screen shows.
void InputTestPage::draw(SDL_Surface* screen, bool) {
    TTF_Font* font             = Fonts_getSmall();
    const SDL_Color primary    = Theme_getColor(THEME_ROLE_PRIMARY, false);
    const SDL_Color secondary  = Theme_getColor(THEME_ROLE_SECONDARY, false);
    const int lineHeight       = TTF_FontLineSkip(font);
    const int heldTop          = SCALE1(PADDING + PILL_SIZE + PADDING);
    const int hintTop          = heldTop + lineHeight;
    const int logTop           = hintTop + lineHeight + SCALE1(PADDING);
    const int bottom           = screen->h - SCALE1(PADDING);

    // The coordinator clears the screen before a draw after a change of the status group,
    // thus the header draws on a clear screen
    const int statusGroupWidth = InputState::getInstance().getStatusGroupWidth();
    if (drawnStatusGroupWidth != statusGroupWidth) {
        render_screen_title(screen, "Input test", statusGroupWidth);
        drawTextLine(screen, font, "MENU + SELECT: back", secondary, hintTop);
        drawnStatusGroupWidth = statusGroupWidth;
    }

    if (drawnHeld != held) {
        char heldText[256] = "Held:";
        for (int index = 0; index < kButtonCount; index++) {
            const Button button = static_cast<Button>(index);
            if (held & getButtonBit(button)) {
                const size_t used = strlen(heldText);
                snprintf(heldText + used, sizeof(heldText) - used, " %s", getButtonName(button));
            }
        }
        clearRows(screen, heldTop, hintTop);
        drawTextLine(screen, font, heldText, primary, heldTop);
        drawnHeld = held;
    }

    if (drawnLogVersion != logVersion) {
        clearRows(screen, logTop, bottom);
        // The newest entry is at the bottom, as in a log
        const int fit   = std::max(0, (bottom - logTop) / lineHeight);
        const int shown = std::min(entryCount, fit);
        int y           = logTop;
        for (int age = shown - 1; age >= 0; age--) {
            const int index = (newest - age + kEntryCount) % kEntryCount;
            y += drawTextLine(screen, font, entries[index].text.c_str(), primary, y);
        }
        drawnLogVersion = logVersion;
    }
}

}  // namespace
}  // namespace ui

ModuleExitReason InputTestModule_run(DisplayContext* display) {
    ui::InputTestPage page;
    return runPageLoop(page, display);
}

#endif
