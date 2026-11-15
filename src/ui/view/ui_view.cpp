#include "ui/view/ui_view.hpp"

#include "ui/view/unique_id.hpp"

UiView::UiView() : id(ui::generateUniqueId()) {}
