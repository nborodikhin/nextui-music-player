#ifndef __UI_VIEW_UI_VIEW_HPP__
#define __UI_VIEW_UI_VIEW_HPP__

#include "ui/view/ui_context.hpp"
#include "ui/view/ui_draw_context.hpp"
#include "ui/view/ui_layout_context.hpp"
#include "ui/view/ui_size.hpp"

class UiView {
public:
    UiView();
    virtual ~UiView();

    UiView(const UiView&)            = delete;
    UiView& operator=(const UiView&) = delete;

    /** Returns an id that no other view of the run has. */
    int getId() const { return id; }

    /**
     * Sets the size and the content rectangle of the view, and lays out its children. A view
     * stays alive until the layout of the page after it leaves the tree of the page.
     */
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
    /**
     * Called when the view attaches to `newContext`, before its layout. An override drops each
     * cache that depends on the screen.
     */
    virtual void onAttach([[maybe_unused]] UiContext& newContext) {}

    UiContext& getContext() const { return *context; }
    bool hasContext() const { return context != nullptr; }

    /**
     * Returns the scheduler of the view. The stack cancels each task of the view after a layout of
     * the page that does not give the view.
     */
    UiTaskScheduler& getScheduler() { return scheduler; }

private:
    friend class UiLayoutContext;
    friend class UiContext;
    friend class UiCompositor;

    /** Attaches the view to `layoutContext` where it is not attached to it for the current screen. */
    void prepareLayout(UiContext& layoutContext);

    /** Gives the view `newContext`, marks the view dirty, and calls onAttach(). */
    void attach(UiContext& newContext);

    /** Cancels each task of the view, and takes its context away. */
    void detach();

    int id;
    UiContext* context = nullptr;
    OwnerScheduler scheduler{id};
    UiSize size        = {0, 0};
    bool ownerVisible  = true;
    /** The view attached after its last draw */
    bool attachDirty = false;
    /** The count of new screens of the context at the attach */
    uint32_t screenCount = 0;
    /** The number of the last layout of the page that reached the view */
    uint32_t layoutNumber = 0;
};

#endif
