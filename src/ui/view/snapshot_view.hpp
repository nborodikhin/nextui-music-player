#ifndef __UI_VIEW_SNAPSHOT_VIEW_HPP__
#define __UI_VIEW_SNAPSHOT_VIEW_HPP__

#include "ui/view/ui_view.hpp"

/**
 * A still picture of the screen with a dim over it. A view with no picture draws nothing.
 */
class SnapshotView : public UiView {
public:
    SnapshotView() = default;
    ~SnapshotView() override;

    /**
     * Takes image, a picture of the screen in premultiplied alpha, and puts dim over it. The
     * view frees image. A null image clears the view.
     */
    void setImage(SDL_Surface* image, SDL_Color dim);

    /** Frees the picture. */
    void clear();

    bool hasImage() const { return image != nullptr; }

    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

private:
    SDL_Surface* image = nullptr;
    bool dirty         = true;
};

#endif
