#ifndef __UI_VIEW_LIST_VIEW_HPP__
#define __UI_VIEW_LIST_VIEW_HPP__

#include <memory>
#include <vector>

#include "ui/view/list_adapter.hpp"
#include "ui/view/list_item_view.hpp"
#include "ui/view/scrolling_view.hpp"
#include "ui/view/ui_view.hpp"

/**
 * Lays out the items of an adapter in a column, and scrolls to keep the selected item
 * visible. Shows whole items only. Sets the selection of each item view.
 */
class ListView : public ScrollingView {
public:
    void setAdapter(ListAdapter& adapter);

    /** Reads the items again. An id that stays keeps its view. */
    void notifyDataChanged();

    /**
     * Moves the selection one item. A move past the last item goes to the first, and back.
     * Returns: true when the selection moved
     */
    bool moveForward();
    bool moveBackward();

    /**
     * Moves the selection by the items that fit in the height of the list.
     * Returns: true when the selection moved, and false also when the heights of the items are
     * not known, until the next layout
     */
    bool pageForward();
    bool pageBackward();

    /** Selects index, which the next layout scrolls into view. */
    void setSelectedIndex(int index);

    int getItemCount() const { return static_cast<int>(items.size()); }

    /** The indexes are -1 for an empty list. */
    int getSelectedIndex() const { return selected; }
    int getFirstVisibleIndex() const { return items.empty() ? -1 : firstVisible; }
    int getLastVisibleIndex() const { return items.empty() ? -1 : lastVisible; }

    ListItemView& getItemView(int index) { return *items[static_cast<size_t>(index)].view; }

    /** Returns the id that the adapter gave for the item at `index`. */
    long getItemId(int index) const { return items[static_cast<size_t>(index)].id; }

    bool canScrollUp() const override { return getFirstVisibleIndex() > 0; }
    bool canScrollDown() const override;
    int getShownBottom() const override;

    void layout(UiLayoutContext& context) override;

private:
    struct Item {
        long id;
        std::shared_ptr<ListItemView> view;
        int height;
    };

    /** Selects index, and sets the selection of each item view. */
    void select(int index);

    int getTop(int index) const;
    int findLastFullyVisible(int first, int height) const;
    bool movePage(int direction);

    ListAdapter* adapter = nullptr;
    std::vector<Item> items;
    // The views that left the list. They stay until the next layout, thus the entries of the
    // frame still hold them.
    std::vector<std::shared_ptr<ListItemView>> leaving;
    bool measured    = false;
    int selected     = -1;
    int firstVisible = 0;
    int lastVisible  = 0;
    int height       = 0;
};

#endif
