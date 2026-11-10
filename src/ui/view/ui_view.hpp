#ifndef __UI_VIEW_UI_VIEW_HPP__
#define __UI_VIEW_UI_VIEW_HPP__

#include "ui/view/ui_context.hpp"
#include "ui/view/ui_draw_context.hpp"
#include "ui/view/ui_layout_context.hpp"
#include "ui/view/ui_size.hpp"

class UiView {
public:
    UiView();
    virtual ~UiView() = default;

    UiView(const UiView&)            = delete;
    UiView& operator=(const UiView&) = delete;

    /** Returns an id that no other view of the run has. */
    int getId() const { return id; }

    /** Drops each cache of the view. An override calls the base and forwards the call to the children. */
    virtual void setContext(UiContext& newContext) { context = &newContext; }

    /** Sets the size and the content rectangle of the view, and lays out its children. */
    virtual void layout(UiLayoutContext&) {}

    /** Returns the size from the last layout. */
    UiSize getSize() const { return size; }

    /** Hides the view and its children until the next call. The owner of the view decides it. */
    void setVisible(bool visible) { ownerVisible = visible; }
    bool isVisible() const { return ownerVisible; }

    /** Returns true when the content changed since the last draw. */
    virtual bool isDirty() const { return false; }

    /**
     * Draws the whole view into the bitmap of the context, in the coordinates of the view. The
     * result does not depend on the position of the view. Clears the dirty flags.
     */
    virtual void draw(UiDrawContext&) {}

protected:
    UiContext& getContext() const { return *context; }
    bool hasContext() const { return context != nullptr; }

private:
    friend class UiLayoutContext;

    int id;
    UiContext* context = nullptr;
    UiSize size        = {0, 0};
    bool ownerVisible  = true;
};

#endif
