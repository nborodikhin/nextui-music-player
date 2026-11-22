#include "ui/view/ui_view.hpp"

#include "ui/view/unique_id.hpp"

UiView::UiView() : id(ui::generateUniqueId()) {}

UiView::~UiView() { detach(); }

void UiView::prepareLayout(UiContext& layoutContext) {
    if (context != &layoutContext || screenCount != layoutContext.getScreenCount()) attach(layoutContext);
    layoutNumber = layoutContext.getLayoutNumber();
}

void UiView::attach(UiContext& newContext) {
    if (context != &newContext) {
        detach();
        context = &newContext;
        newContext.addView(*this);
    }
    scheduler.setContext(newContext);
    screenCount = newContext.getScreenCount();
    attachDirty = true;
    onAttach(newContext);
}

void UiView::detach() {
    if (!context) return;
    UiContext* oldContext = context;
    context               = nullptr;
    oldContext->cancelOwnerTasks(id);
    oldContext->removeView(*this);
    scheduler.clearContext();
}

void UiContext::detachView(UiView& view) { view.detach(); }

bool UiContext::isLaidOut(const UiView& view, uint32_t layoutNumber) { return view.layoutNumber == layoutNumber; }
