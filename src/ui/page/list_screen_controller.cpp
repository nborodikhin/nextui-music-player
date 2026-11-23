#include "ui/page/list_screen_controller.hpp"

#include <array>
#include <optional>
#include <variant>

#include "ui_utils.h"

using namespace input;

namespace {

constexpr std::array<ButtonRequest, 8> kRequests = {
    request::Repeat{Button::DpadUp},
    request::Repeat{Button::DpadDown},
    request::Repeat{Button::DpadLeft},
    request::Repeat{Button::DpadRight},
    request::Repeat{Button::AnalogUp},
    request::Repeat{Button::AnalogDown},
    request::Repeat{Button::AnalogLeft},
    request::Repeat{Button::AnalogRight},
};

/** Returns the button of a short press or a repeat. */
std::optional<Button> getPressedButton(const ButtonEvent& event) {
    if (const auto* typedEvent = std::get_if<event::ShortPress>(&event.kind)) return typedEvent->button;
    if (const auto* typedEvent = std::get_if<event::Repeat>(&event.kind)) return typedEvent->button;
    return std::nullopt;
}

/** Puts the text of `view` at rest, and starts the wait before it scrolls. */
template <class View>
void restartScrolling(UiTaskScheduler& scheduler, View& view, TaskId& start) {
    view.stopScrolling();
    view.resetScrolling();
    scheduler.cancelTask(start);
    start = scheduler.postDelayed([&view] { view.startScrolling(); }, SCROLL_START_DELAY);
}

}  // namespace

ListScreenController::ListScreenController(ListScreenView& view, UiTaskScheduler& scheduler,
                                           ListScreenListener& listener)
    : view(view), scheduler(scheduler), listener(listener) {}

std::span<const ButtonRequest> ListScreenController::getButtonRequests() {
    return kRequests;
}

void ListScreenController::start() {
    restartScrolling(scheduler, view.getTitleView(), titleStart);
    shownIndex = -1;
    showSelection();
}

bool ListScreenController::onButtonEvent(const ButtonEvent& event) {
    const std::optional<Button> button = getPressedButton(event);
    if (!button) return false;
    const bool isRepeat = std::holds_alternative<event::Repeat>(event.kind);
    ListView& list      = view.listView;
    bool moved          = false;
    switch (*button) {
        case Button::DpadUp:
        case Button::AnalogUp:   moved = list.moveBackward(); break;
        case Button::DpadDown:
        case Button::AnalogDown: moved = list.moveForward(); break;
        case Button::DpadLeft:
        case Button::AnalogLeft:
        case Button::DpadRight:
        case Button::AnalogRight: {
            const bool forward = *button == Button::DpadRight || *button == Button::AnalogRight;
            const HorizontalDirection direction = forward ? HorizontalDirection::Right : HorizontalDirection::Left;
            const int position = list.getSelectedIndex();
            if (position >= 0 && listener.onLeftRight(position, direction, isRepeat)) return true;
            moved = forward ? list.pageForward() : list.pageBackward();
            break;
        }
        default: return false;
    }
    if (moved) showSelection();
    return true;
}

void ListScreenController::notifyDataChanged() {
    view.listView.notifyDataChanged();
    showSelection();
}

int ListScreenController::getSelectedIndex() const {
    return view.listView.getSelectedIndex();
}

void ListScreenController::setSelectedIndex(int index) {
    view.listView.setSelectedIndex(index);
    showSelection();
}

int ListScreenController::getItemCount() const {
    return view.listView.getItemCount();
}

ListItemView& ListScreenController::getItemView(int index) {
    return view.listView.getItemView(index);
}

void ListScreenController::restartRowScrolling() {
    ListView& list  = view.listView;
    const int index = list.getSelectedIndex();
    if (index < 0) {
        scheduler.cancelTask(rowStart);
        return;
    }
    restartScrolling(scheduler, list.getItemView(index), rowStart);
}

void ListScreenController::showSelection() {
    const ListView& list = view.listView;
    const int index      = list.getSelectedIndex();
    const long id        = index >= 0 ? list.getItemId(index) : 0;
    if (index == shownIndex && id == shownId) return;
    const bool sameItem = shownIndex >= 0 && index >= 0 && id == shownId;
    shownIndex          = index;
    shownId             = id;
    // The row keeps its scroll when only its position changes
    if (!sameItem) restartRowScrolling();
    listener.onSelectionChanged(index);
}
