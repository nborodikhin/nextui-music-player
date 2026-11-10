#include "ui/view/ui_view.hpp"

namespace {

int nextViewId = 1;

}  // namespace

UiView::UiView() : id(nextViewId++) {}
