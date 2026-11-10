#include "ui/view/title_text_view.hpp"

#include <algorithm>
#include <cstdint>

void TitleTextView::finishDraw(SDL_Surface* bitmap) {
    if (isScrolling() || !isClipped()) return;
    // The text washes out over one em at the cut. The pixels are premultiplied, thus each
    // channel takes the same factor.
    const int fadeWidth = std::min(getFontHeight(), bitmap->w);
    const int start     = bitmap->w - fadeWidth;
    for (int y = 0; y < bitmap->h; y++) {
        uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(bitmap->pixels) + y * bitmap->pitch);
        for (int x = start; x < bitmap->w; x++) {
            const uint32_t keep = static_cast<uint32_t>(255 * (bitmap->w - x) / fadeWidth);
            uint32_t pixel      = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                pixel |= (((row[x] >> shift) & 0xFF) * keep / 255) << shift;
            }
            row[x] = pixel;
        }
    }
}
