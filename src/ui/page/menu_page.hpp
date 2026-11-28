#ifndef __UI_PAGE_MENU_PAGE_HPP__
#define __UI_PAGE_MENU_PAGE_HPP__

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "help_screen.h"
#include "input/input_events.hpp"
#include "ui/page/list_screen_controller.hpp"
#include "ui/page/screen_page.hpp"
#include "ui/view/list_adapter.hpp"
#include "ui/view/list_screen_view.hpp"

class MenuRowView;

/** A row of a menu. A row with a value changes it with LEFT and RIGHT, and each other row acts on A. */
struct MenuRow {
    long id = 0;
    std::string label;
    /** A smaller text at the right of the row. Empty for none. */
    std::string secondaryText;
    /** The value at the right, between "<" and ">" while the row is selected. Empty for none. */
    std::string value;
    std::function<void()> onSelect;
    std::function<void(HorizontalDirection)> onChange;
};

/** What the owner of the list does when a MenuAdapter reads its rows again. */
class MenuAdapterListener {
public:
    /** Called when the ids of the rows changed. The owner reads the items of the list again. */
    virtual void onRowsChanged() = 0;
    /** Called when a text of the row at `index` changed. */
    virtual void onRowChanged(int index) = 0;

protected:
    ~MenuAdapterListener() = default;
};

/** The rows of a menu. A subclass gives the ids of the rows, and each row as it is now. */
class MenuAdapter : public ListAdapter {
public:
    /** Tells `listener` of each reload. Pass nullptr in `listener` for none. */
    void setListener(MenuAdapterListener* listener) { this->listener = listener; }

    /** Reads the ids and each row again. */
    void reload();
    /** Reads the row with `id` again. The list does not read its items again. */
    void reload(long id);

    const MenuRow& getRow(int index) const { return rows[static_cast<size_t>(index)]; }

    int getCount() const final { return static_cast<int>(rows.size()); }
    long getItemId(int index) const final { return getRow(index).id; }
    std::shared_ptr<ListItemView> getView(int index) final;

protected:
    ~MenuAdapter() = default;

    virtual std::vector<long> loadRowIds() const = 0;
    virtual MenuRow loadRow(long id) const = 0;

private:
    /** Shows the row at `index` in its view, when the view exists. Returns: true when a text changed */
    bool showRow(int index);

    std::vector<MenuRow> rows;
    /** The view of each id, kept while the id is a row, thus a row keeps its scroll */
    std::map<long, std::shared_ptr<MenuRowView>> views;
    MenuAdapterListener* listener = nullptr;
};

/** A menu with fixed rows. */
class ArrayMenuAdapter final : public MenuAdapter {
public:
    /** Keeps `rows` and reads them again. */
    void setRows(std::vector<MenuRow> rows);

protected:
    std::vector<long> loadRowIds() const override;
    MenuRow loadRow(long id) const override;

private:
    std::vector<MenuRow> fixedRows;
};

/**
 * A menu screen: A acts on the selected row, LEFT and RIGHT change its value, and B goes back.
 * The hints show A OPEN, or LEFT/RIGHT CHANGE on a row with a value.
 */
template <class Page>
class MenuPageImpl : public ScreenPageImpl<Page, ListScreenView>,
                     private ListScreenListener,
                     private MenuAdapterListener {
protected:
    MenuPageImpl(const std::string& title, HelpId helpId) : ScreenPageImpl<Page, ListScreenView>(title), helpId(helpId) {
        showHints();
    }

    /** Returns the repeats of the cursor buttons. A page with more requests adds these to its own. */
    std::span<const input::ButtonRequest> getButtonRequests() const override {
        return ListScreenController::getButtonRequests();
    }

    /** Returns the help id of the constructor. */
    HelpId getHelpId() const override { return helpId; }

    /** Shows the rows of `adapter`, which the page keeps alive, and starts the title and the rows. */
    void start(MenuAdapter& newAdapter) {
        adapter = &newAdapter;
        adapter->reload();
        adapter->setListener(this);
        this->getScreenView().setAdapter(newAdapter);
        list.start();
        showHints();
    }

    /** Returns the selected row, or null for an empty menu. */
    const MenuRow* getSelectedRow() const {
        if (!adapter) return nullptr;
        const int index = list.getSelectedIndex();
        if (index < 0 || index >= adapter->getCount()) return nullptr;
        return &adapter->getRow(index);
    }

    /** Sets the text of the B hint, before the page shows. The default is BACK. */
    void setBackText(const char* text) {
        openHints[1]   = text;
        changeHints[1] = text;
    }

    /** Called for B. The default closes the page. */
    virtual void onBack() { this->close(); }

    /** A page with more buttons handles them first, and then calls this. */
    bool onButtonEvent(const input::ButtonEvent& event) override {
        if (list.onButtonEvent(event)) return true;
        if (event.isSingleButton(input::Button::A)) {
            selectRow();
            return true;
        }
        if (event.isSingleButton(input::Button::B)) {
            onBack();
            return true;
        }
        return false;
    }

private:
    static constexpr const char* kLeftHints[] = {"START", "CONTROLS", nullptr};

    /** A on a row with a value changes it, as RIGHT does */
    void selectRow() {
        const MenuRow* row = getSelectedRow();
        if (!row) return;
        if (row->onChange) {
            changeRow(*row, HorizontalDirection::Right);
        } else if (row->onSelect) {
            row->onSelect();
        }
    }

    /** A value can change the label of another row, thus each row is read again */
    void changeRow(const MenuRow& row, HorizontalDirection direction) {
        // The reload replaces the row, thus the callback is a copy
        const std::function<void(HorizontalDirection)> change = row.onChange;
        change(direction);
        adapter->reload();
    }

    /** A row with a value changes it once for each press. Each other row lets the list page. */
    bool onLeftRight(int listItemPosition, HorizontalDirection direction, bool isRepeat) override {
        if (!adapter || listItemPosition >= adapter->getCount()) return false;
        const MenuRow& row = adapter->getRow(listItemPosition);
        if (!row.onChange) return false;
        if (!isRepeat) changeRow(row, direction);
        return true;
    }

    void onSelectionChanged(int) override { showHints(); }

    void onRowsChanged() override { list.notifyDataChanged(); }

    void onRowChanged(int index) override {
        if (index == list.getSelectedIndex()) list.restartRowScrolling();
    }

    /** The view of the hints keeps the arrays, and takes a new array only, thus each state has its own */
    void showHints() {
        const MenuRow* row  = getSelectedRow();
        const bool hasValue = row && row->onChange;
        this->getScreenView().getButtonHintsView().setHints(kLeftHints, hasValue ? changeHints.data() : openHints.data());
    }

    HelpId helpId;
    std::array<const char*, 5> openHints   = {"B", "BACK", "A", "OPEN", nullptr};
    std::array<const char*, 5> changeHints = {"B", "BACK", "LEFT/RIGHT", "CHANGE", nullptr};
    MenuAdapter* adapter = nullptr;
    ListScreenController list{this->getScreenView(), this->getScheduler(), *this};
};

#endif
