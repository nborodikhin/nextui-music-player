#ifndef __UI_VIEW_BITMAP_VIEW_HPP__
#define __UI_VIEW_BITMAP_VIEW_HPP__

#include <cstddef>

#include "ui/view/ui_view.hpp"

/**
 * A bitmap with straight alpha, scaled to the size of the view. A view with no bitmap draws
 * nothing.
 */
class BitmapView : public UiView {
public:
    BitmapView() = default;
    ~BitmapView() override;

    /** Takes `bitmap`, and frees it with the view. A null bitmap clears the view. */
    void setBitmap(SDL_Surface* bitmap);

    /**
     * Shows the image in the file at `path`, in a format that SDL_image reads.
     * Returns: false when the image does not load, and the view then shows no bitmap
     */
    bool loadFile(const char* path);

    /**
     * Shows the image in the `size` bytes at `data`, in a format that SDL_image reads. The view
     * does not keep `data`.
     * Returns: false when the image does not load, and the view then shows no bitmap
     */
    bool loadMemory(const void* data, size_t size);

    bool hasBitmap() const { return bitmap != nullptr; }

    bool isDirty() const override { return dirty; }
    void draw(UiDrawContext& context) override;

private:
    SDL_Surface* bitmap = nullptr;
    bool dirty          = true;
};

#endif
