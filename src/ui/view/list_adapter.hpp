#ifndef __UI_VIEW_LIST_ADAPTER_HPP__
#define __UI_VIEW_LIST_ADAPTER_HPP__

#include <memory>

#include "ui/view/ui_view.hpp"

/** Gives a list its items. */
class ListAdapter {
public:
    virtual int getCount() const = 0;

    /** Returns an id that stays with the item when the data changes. */
    virtual long getItemId(int index) const = 0;

    /** Returns the view of the item. The adapter can make it, or return a view that it keeps. */
    virtual std::shared_ptr<UiView> getView(int index) = 0;

protected:
    ~ListAdapter() = default;
};

#endif
