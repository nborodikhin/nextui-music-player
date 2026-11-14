#include "ui/view/snapshot_view.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

uint32_t* pixelAt(SDL_Surface* surface, int x, int y) {
    return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels) + y * surface->pitch) + x;
}

// Puts the color over each pixel of a premultiplied image
void putDimOver(SDL_Surface* image, SDL_Color dim) {
    const uint32_t keep     = 255 - dim.a;
    const auto premultiply  = [&dim](uint8_t channel) { return static_cast<uint32_t>((channel * dim.a + 127) / 255); };
    const uint32_t dimPixel = (static_cast<uint32_t>(dim.a) << 24) | (premultiply(dim.r) << 16) |
                              (premultiply(dim.g) << 8) | premultiply(dim.b);
    for (int y = 0; y < image->h; y++) {
        uint32_t* row = pixelAt(image, 0, y);
        for (int x = 0; x < image->w; x++) {
            uint32_t result = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const uint32_t channel = ((dimPixel >> shift) & 0xFF) + (((row[x] >> shift) & 0xFF) * keep + 127) / 255;
                result |= std::min(channel, 255u) << shift;
            }
            row[x] = result;
        }
    }
}

}  // namespace

SnapshotView::~SnapshotView() { clear(); }

void SnapshotView::setImage(SDL_Surface* newImage, SDL_Color dim) {
    clear();
    image = newImage;
    if (image) putDimOver(image, dim);
    dirty = true;
}

void SnapshotView::clear() {
    if (!image) return;
    SDL_FreeSurface(image);
    image = nullptr;
    dirty = true;
}

void SnapshotView::draw(UiDrawContext& context) {
    dirty = false;
    if (!image) return;
    SDL_Surface* surface = context.getSurface();
    if (!surface) return;
    const int width  = std::min(surface->w, image->w);
    const int height = std::min(surface->h, image->h);
    for (int y = 0; y < height; y++) {
        std::memcpy(pixelAt(surface, 0, y), pixelAt(image, 0, y), static_cast<size_t>(width) * sizeof(uint32_t));
    }
}
