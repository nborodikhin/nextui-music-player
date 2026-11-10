#include "ui/view/list_view.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

void ListView::setAdapter(ListAdapter& newAdapter) {
    adapter = &newAdapter;
    notifyDataChanged();
}

void ListView::notifyDataChanged() {
    if (!adapter) return;
    const long selectedId = selected >= 0 ? items[static_cast<size_t>(selected)].id : 0;
    const bool hadSelection = selected >= 0;

    std::unordered_map<long, std::shared_ptr<UiView>> kept;
    for (Item& item : items) kept[item.id] = std::move(item.view);
    items.clear();
    const int count = adapter->getCount();
    for (int index = 0; index < count; index++) {
        const long id = adapter->getItemId(index);
        const auto it = kept.find(id);
        std::shared_ptr<UiView> view;
        if (it != kept.end()) {
            view = std::move(it->second);
            kept.erase(it);
        } else {
            view = adapter->getView(index);
            if (hasContext()) view->setContext(getContext());
        }
        items.push_back(Item{id, std::move(view), 0});
    }
    for (auto& [id, view] : kept) leaving.push_back(std::move(view));

    measured = false;
    selected = items.empty() ? -1 : std::clamp(selected, 0, count - 1);
    if (hadSelection) {
        for (int index = 0; index < count; index++) {
            if (items[static_cast<size_t>(index)].id == selectedId) selected = index;
        }
    }
}

bool ListView::moveForward() {
    if (getItemCount() < 2) return false;
    selected = (selected + 1) % getItemCount();
    return true;
}

bool ListView::moveBackward() {
    if (getItemCount() < 2) return false;
    selected = (selected + getItemCount() - 1) % getItemCount();
    return true;
}

bool ListView::pageForward() { return movePage(1); }

bool ListView::pageBackward() { return movePage(-1); }

bool ListView::movePage(int direction) {
    if (!measured || selected < 0) return false;
    // One page is the height of the list, from the selected item
    int moved   = 0;
    int target  = selected;
    while (true) {
        const int next = target + direction;
        if (next < 0 || next >= getItemCount()) break;
        moved += items[static_cast<size_t>(next)].height;
        if (moved > height) break;
        target = next;
    }
    if (target == selected) return false;
    selected = target;
    return true;
}

void ListView::setSelectedIndex(int index) {
    selected = items.empty() ? -1 : std::clamp(index, 0, getItemCount() - 1);
}

void ListView::setContext(UiContext& context) {
    UiView::setContext(context);
    for (Item& item : items) item.view->setContext(context);
}

int ListView::getTop(int index) const {
    int top = 0;
    for (int i = 0; i < index; i++) top += items[static_cast<size_t>(i)].height;
    return top;
}

int ListView::findLastFullyVisible(int first, int listHeight) const {
    int last   = first;
    int bottom = items[static_cast<size_t>(first)].height;
    while (last + 1 < getItemCount() && bottom + items[static_cast<size_t>(last + 1)].height <= listHeight) {
        last++;
        bottom += items[static_cast<size_t>(last)].height;
    }
    return last;
}

void ListView::layout(UiLayoutContext& context) {
    leaving.clear();
    const UiSize size = context.getSize();
    height            = size.h;
    if (items.empty()) return;

    if (!measured) {
        UiLayoutContext measuring = context.createMeasuringContext();
        for (Item& item : items) {
            item.height = measuring.layoutChild(*item.view, SDL_Rect{0, 0, size.w, size.h}).getSize().h;
        }
        measured = true;
    }

    firstVisible = std::clamp(firstVisible, 0, getItemCount() - 1);
    if (selected < firstVisible) firstVisible = selected;
    while (selected > findLastFullyVisible(firstVisible, size.h)) firstVisible++;
    lastVisible = findLastFullyVisible(firstVisible, size.h);

    const int offset = getTop(firstVisible);
    int top          = 0;
    for (Item& item : items) {
        const int y            = top - offset;
        UiLayoutContext& child = context.layoutChild(*item.view, SDL_Rect{0, y, size.w, item.height});
        // A list shows whole items only
        if (y < 0 || y + item.height > size.h) child.setVisible(false);
        top += item.height;
    }
}
