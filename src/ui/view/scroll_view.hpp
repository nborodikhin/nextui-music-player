#ifndef __UI_VIEW_SCROLL_VIEW_HPP__
#define __UI_VIEW_SCROLL_VIEW_HPP__

#include <span>

#include "input/input_events.hpp"
#include "ui/view/scrolling_view.hpp"

/**
 * Shows a part of one content view that can be higher than this view. The content gets the
 * width of this view and the height that it needs. The offset stays in the range where the last
 * line of the content is not above the bottom of this view.
 */
class ScrollView : public ScrollingView {
public:
    /** Shows `content`, which the caller keeps alive, from its top. */
    void setContent(UiView* content);

    /** Moves the content up by `pixels`, or down for a negative value. */
    void scrollBy(int pixels);

    /** Moves the content up by `pages` heights of this view, or down for a negative value. */
    void scrollPage(int pages);

    /** Sets the move of up and down, in pixels. The default is the line height of the medium font. */
    void setLineHeight(int pixels) { lineHeight = pixels; }

    /** Returns the repeats of the buttons that handleButton() uses. */
    static std::span<const input::ButtonRequest> getButtonRequests();

    /**
     * Scrolls for a press or a repeat of the D-pad: up and down by a line, left and right by a page.
     * Returns: true when the event is a button that scrolls
     */
    bool handleButton(const input::ButtonEvent& event);

    bool canScrollUp() const override { return offset > 0; }
    bool canScrollDown() const override { return offset < getMaxOffset(); }
    int getShownBottom() const override { return viewportHeight; }

    void layout(UiLayoutContext& context) override;

private:
    int getMaxOffset() const;

    UiView* content    = nullptr;
    int lineHeight     = 0;
    int offset         = 0;
    int contentHeight  = 0;
    int viewportHeight = 0;
};

#endif
