#include "ui/page/menu_page.hpp"

#include <algorithm>
#include <utility>

#include "ui/view/list_row_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui_fonts.h"
#include "ui_theme.h"

/**
 * The row of a menu: the label, and at its right the value or the secondary text. The value of
 * the selected row is between "<" and ">".
 */
class MenuRowView final : public ListRowView {
public:
    /** Shows `row`. Returns: true when a text changed */
    bool show(const MenuRow& row) {
        bool changed = setText(row.label);
        changed      = setRightText(secondaryView, row.secondaryText, Fonts_getTiny()) || changed;
        secondary    = row.secondaryText;
        changed      = (value != row.value) || changed;
        value        = row.value;
        showValue();
        return changed;
    }

    void setSelected(bool selected) override {
        ListRowView::setSelected(selected);
        showValue();
    }

private:
    /** Sets the text of `view`, and makes the view in `font` for the first text. Returns: true when the text changed */
    bool setRightText(std::shared_ptr<TextView>& view, const std::string& text, TTF_Font* font) {
        if (!view && text.empty()) return false;
        if (!view) {
            view = std::make_shared<TextView>(font, false);
            view->setColor(Theme_getColor(THEME_ROLE_PRIMARY, false));
        }
        return view->setText(text);
    }

    /** The value takes the place of the secondary text at the right edge */
    void showValue() {
        const std::string shown = isSelected() && !value.empty() ? "< " + value + " >" : value;
        setRightText(valueView, shown, Fonts_getLarge());
        if (!value.empty()) {
            setRightView(valueView);
        } else if (!secondary.empty()) {
            setRightView(secondaryView);
        } else {
            setRightView(nullptr);
        }
    }

    std::string value;
    std::string secondary;
    std::shared_ptr<TextView> valueView;
    std::shared_ptr<TextView> secondaryView;
};

void MenuAdapter::reload() {
    std::vector<long> ids = loadRowIds();
    std::vector<MenuRow> newRows;
    newRows.reserve(ids.size());
    for (long id : ids) newRows.push_back(loadRow(id));

    bool idsChanged = ids.size() != rows.size();
    for (size_t index = 0; !idsChanged && index < ids.size(); index++) idsChanged = ids[index] != rows[index].id;
    rows = std::move(newRows);
    std::erase_if(views, [&ids](const auto& entry) { return std::find(ids.begin(), ids.end(), entry.first) == ids.end(); });

    // A view that the list does not hold yet shows its row when the list asks for it
    std::vector<int> changedRows;
    for (int index = 0; index < getCount(); index++) {
        if (showRow(index)) changedRows.push_back(index);
    }
    if (!listener) return;
    if (idsChanged) listener->onRowsChanged();
    for (int index : changedRows) listener->onRowChanged(index);
}

void MenuAdapter::reload(long id) {
    for (int index = 0; index < getCount(); index++) {
        if (rows[static_cast<size_t>(index)].id != id) continue;
        rows[static_cast<size_t>(index)] = loadRow(id);
        if (showRow(index) && listener) listener->onRowChanged(index);
        return;
    }
}

std::shared_ptr<ListItemView> MenuAdapter::getView(int index) {
    std::shared_ptr<MenuRowView>& view = views[getItemId(index)];
    if (!view) {
        view = std::make_shared<MenuRowView>();
        view->show(getRow(index));
    }
    return view;
}

bool MenuAdapter::showRow(int index) {
    const auto view = views.find(getItemId(index));
    if (view == views.end()) return false;
    return view->second->show(getRow(index));
}

void ArrayMenuAdapter::setRows(std::vector<MenuRow> newRows) {
    fixedRows = std::move(newRows);
    reload();
}

std::vector<long> ArrayMenuAdapter::loadRowIds() const {
    std::vector<long> ids;
    ids.reserve(fixedRows.size());
    for (const MenuRow& row : fixedRows) ids.push_back(row.id);
    return ids;
}

MenuRow ArrayMenuAdapter::loadRow(long id) const {
    for (const MenuRow& row : fixedRows) {
        if (row.id == id) return row;
    }
    return {};
}
