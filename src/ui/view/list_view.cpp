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

    std::unordered_map<long, std::shared_ptr<ListItemView>> kept;
    for (Item& item : items) kept[item.id] = std::move(item.view);
    items.clear();
    const int count = adapter->getCount();
    for (int index = 0; index < count; index++) {
        const long id = adapter->getItemId(index);
        const auto it = kept.find(id);
        std::shared_ptr<ListItemView> view;
        if (it != kept.end()) {
            view = std::move(it->second);
            kept.erase(it);
        } else {
            view = adapter->getView(index);
        }
        items.push_back(Item{id, std::move(view), 0});
    }
    for (auto& [id, view] : kept) leaving.push_back(std::move(view));

    measured     = false;
    int newIndex = items.empty() ? -1 : std::clamp(selected, 0, count - 1);
    if (hadSelection) {
        for (int index = 0; index < count; index++) {
            if (items[static_cast<size_t>(index)].id == selectedId) newIndex = index;
        }
    }
    select(newIndex);
}

void ListView::select(int index) {
    selected = index;
    for (int i = 0; i < getItemCount(); i++) items[static_cast<size_t>(i)].view->setSelected(i == selected);
}

bool ListView::moveForward() {
    if (getItemCount() < 2) return false;
    select((selected + 1) % getItemCount());
    return true;
}

bool ListView::moveBackward() {
    if (getItemCount() < 2) return false;
    select((selected + getItemCount() - 1) % getItemCount());
    return true;
}

bool ListView::pageForward() { return movePage(1); }

bool ListView::pageBackward() { return movePage(-1); }

bool ListView::movePage(int direction) {
    if (!measured || selected < 0) return false;
    const int last = getItemCount() - 1;
    const int edge = direction > 0 ? last : 0;
    // The window shows the end of the list: only the cursor moves
    if ((direction > 0 ? lastVisible : firstVisible) == edge) {
        if (selected == edge) return false;
        select(edge);
        return true;
    }

    // A full page past the window, or else the full window at the end of the list
    const int newFirst = direction > 0 ? std::min(lastVisible + 1, findFirstFullyVisible(last, height))
                                       : findFirstFullyVisible(firstVisible - 1, height);
    const int cursorTop = getTop(selected) - getTop(firstVisible);
    firstVisible        = newFirst;
    lastVisible         = findLastFullyVisible(newFirst, height);

    // The cursor keeps its place on the screen: the item that covers its old top
    const int windowTop = getTop(newFirst);
    int target          = newFirst;
    while (target < lastVisible && getTop(target + 1) - windowTop <= cursorTop) target++;
    select(target);
    return true;
}

void ListView::setSelectedIndex(int index) {
    select(items.empty() ? -1 : std::clamp(index, 0, getItemCount() - 1));
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

int ListView::findFirstFullyVisible(int last, int listHeight) const {
    int first = last;
    int top   = items[static_cast<size_t>(last)].height;
    while (first > 0 && top + items[static_cast<size_t>(first - 1)].height <= listHeight) {
        first--;
        top += items[static_cast<size_t>(first)].height;
    }
    return first;
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
