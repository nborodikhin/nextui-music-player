#ifndef __UI_VIEW_UI_SIZE_HPP__
#define __UI_VIEW_UI_SIZE_HPP__

/** A size in pixels. */
struct UiSize {
    int w;
    int h;

    bool operator==(const UiSize&) const = default;
};

#endif
