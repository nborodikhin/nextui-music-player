#ifndef __UI_VIEW_PROGRESS_BAR_VIEW_HPP__
#define __UI_VIEW_PROGRESS_BAR_VIEW_HPP__

#include "ui/view/text_view.hpp"
#include "ui/view/ui_view.hpp"

/** The track and the fill of a progress bar. */
class ProgressTrackView : public UiView {
public:
    /** Sets the part of the track that the fill covers. A value outside 0 to 100 is clamped. */
    void setPercent(int percent);
    int getPercent() const { return percent; }

    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

private:
    int percent = 0;
    bool dirty  = true;
};

/**
 * A progress bar across the width that the parent gives, with the percent in its middle. The view
 * is as high as the bar, or as the line of the percent where that line is higher.
 */
class ProgressBarView : public UiView {
public:
    /** Pass the height of the bar in barHeight, in pixels. */
    ProgressBarView(TTF_Font* font, int barHeight);

    /** A value outside 0 to 100 is clamped. */
    void setPercent(int percent);

    void layout(UiLayoutContext& context) override;

private:
    int barHeight;
    ProgressTrackView track;
    TextView label;
};

#endif
