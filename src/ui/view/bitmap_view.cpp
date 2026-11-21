#include "ui/view/bitmap_view.hpp"

#include <SDL2/SDL_image.h>

BitmapView::~BitmapView() { SDL_FreeSurface(bitmap); }

void BitmapView::setBitmap(SDL_Surface* newBitmap) {
    if (newBitmap == bitmap) return;
    SDL_FreeSurface(bitmap);
    bitmap = newBitmap;
    dirty  = true;
}

bool BitmapView::loadFile(const char* path) {
    setBitmap(path ? IMG_Load(path) : nullptr);
    return bitmap != nullptr;
}

bool BitmapView::loadMemory(const void* data, size_t size) {
    SDL_RWops* stream = data ? SDL_RWFromConstMem(data, static_cast<int>(size)) : nullptr;
    // IMG_Load_RW closes the stream
    setBitmap(stream ? IMG_Load_RW(stream, 1) : nullptr);
    return bitmap != nullptr;
}

void BitmapView::draw(UiDrawContext& context) {
    dirty = false;
    if (!bitmap) return;
    SDL_Surface* surface = context.getSurface();
    if (!surface) return;
    SDL_SetSurfaceBlendMode(bitmap, SDL_BLENDMODE_BLEND);
    SDL_BlitScaled(bitmap, nullptr, surface, nullptr);
}
