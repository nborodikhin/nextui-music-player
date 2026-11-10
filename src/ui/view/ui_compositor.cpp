#include "ui/view/ui_compositor.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_set>

#include "ui_layers.h"

namespace {

bool isEmpty(const SDL_Rect& rect) { return rect.w <= 0 || rect.h <= 0; }

bool isSameRect(const SDL_Rect& a, const SDL_Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

SDL_Rect intersect(const SDL_Rect& a, const SDL_Rect& b) {
    SDL_Rect result;
    if (!SDL_IntersectRect(&a, &b, &result)) return SDL_Rect{0, 0, 0, 0};
    return result;
}

uint32_t* pixelAt(SDL_Surface* surface, int x, int y) {
    return reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels) + y * surface->pitch) + x;
}

// How the platform blends a target over the targets below it
enum class TargetBlend {
    // The target replaces what is below it, thus only its opaque pixels show correctly
    Replace,
    Premultiplied,
    Straight,
};

struct TargetInfo {
    UiTargetLayer target;
    // The layer of the target. The regular surface has none.
    std::optional<UiLayer> layer;
    TargetBlend blend;
};

constexpr std::array<TargetInfo, kUiTargetLayerCount> kTargets = {{
    {UiTargetLayer::BackgroundLayer, UI_LAYER_BACKGROUND, TargetBlend::Replace},
    {UiTargetLayer::SelectionLayer, UI_LAYER_SELECTION, TargetBlend::Premultiplied},
    {UiTargetLayer::MainSurface, std::nullopt, TargetBlend::Premultiplied},
    {UiTargetLayer::StatusLayer, UI_LAYER_STATUS, TargetBlend::Straight},
    {UiTargetLayer::AnimationLayer, UI_LAYER_ANIMATION, TargetBlend::Premultiplied},
    {UiTargetLayer::ToastLayer, UI_LAYER_TOAST, TargetBlend::Straight},
}};

constexpr size_t toIndex(UiTargetLayer target) { return static_cast<size_t>(target); }

constexpr bool isTableInOrder() {
    for (size_t i = 0; i < kTargets.size(); i++) {
        if (toIndex(kTargets[i].target) != i) return false;
    }
    return true;
}
static_assert(isTableInOrder(), "kTargets holds each target at the index of its value");

const TargetInfo& getTargetInfo(UiTargetLayer target) { return kTargets[toIndex(target)]; }

// Makes the bitmap of the view on the first call of getSurface(), thus a draw that does not
// call it leaves the view with no bitmap
class BitmapDrawContext : public UiDrawContext {
public:
    BitmapDrawContext(SDL_Surface* kept, UiSize size) : kept(kept), size(size) {}

    SDL_Surface* getSurface() override {
        if (!accessed) {
            accessed = true;
            if (kept && kept->w == size.w && kept->h == size.h) {
                bitmap = kept;
            } else {
                bitmap = SDL_CreateRGBSurfaceWithFormat(0, size.w, size.h, 32, SDL_PIXELFORMAT_ARGB8888);
            }
            if (bitmap) SDL_FillRect(bitmap, nullptr, 0);
        }
        return bitmap;
    }

    void setContentRect(const SDL_Rect& rect) override {
        contentRect    = rect;
        contentRectSet = true;
    }

    /** Returns the bitmap of the draw, or null for a draw with no content. */
    SDL_Surface* getBitmap() const { return bitmap; }

    SDL_Rect getContentRect() const { return contentRectSet ? contentRect : SDL_Rect{0, 0, size.w, size.h}; }

private:
    SDL_Surface* kept;
    UiSize size;
    SDL_Surface* bitmap  = nullptr;
    bool accessed        = false;
    SDL_Rect contentRect = {0, 0, 0, 0};
    bool contentRectSet  = false;
};

}  // namespace

UiCompositor::~UiCompositor() { dropBitmaps(); }

void UiCompositor::setScreen(SDL_Surface* newScreen) {
    screen = newScreen;
    dropBitmaps();
    invalidateAll();
}

SDL_PixelFormat* UiCompositor::getPixelFormat() const { return screen ? screen->format : nullptr; }

UiSize UiCompositor::getScreenSize() const {
    if (!screen) return UiSize{0, 0};
    return UiSize{screen->w, screen->h};
}

UiLayoutContext& UiCompositor::beginLayout() {
    root.emplace(UiLayoutContext::makeRoot(getScreenSize(), surfaceOnly));
    return *root;
}

void UiCompositor::invalidateAll() {
    lastShown.clear();
    composeAll         = true;
    clearLayersPending = true;
}

void UiCompositor::clearUsedLayers() {
    for (const TargetInfo& info : kTargets) {
        if (!info.layer || !usedTargets[toIndex(info.target)]) continue;
        UiLayer_clear(*info.layer);
        layersChanged = true;
    }
    usedTargets        = {};
    clearLayersPending = false;
}

void UiCompositor::setSurfaceOnly(bool newSurfaceOnly) {
    surfaceOnly = newSurfaceOnly;
    invalidateAll();
}

void UiCompositor::collectEntries(const UiLayoutContext& context, bool parentShown) {
    UiView* view     = context.getView();
    const bool shown = parentShown && context.isVisible() && (view == nullptr || view->isVisible());
    if (view) {
        const SDL_Rect screenRect = context.getScreenRect();
        const SDL_Rect clipRect   = context.getClipRect();
        entries.push_back(Entry{
            .view        = view,
            .viewId      = view->getId(),
            .target      = context.getTargetLayer(),
            .screenRect  = screenRect,
            .clipRect    = clipRect,
            .visibleRect = {0, 0, 0, 0},
            .shown       = shown && !isEmpty(intersect(screenRect, clipRect)),
            .needsDraw   = false,
        });
    }
    for (const UiLayoutContext& child : context.getChildren()) {
        collectEntries(child, shown);
    }
}

void UiCompositor::addDamage(UiTargetLayer target, const SDL_Rect& rect) {
    if (isEmpty(rect)) return;
    std::vector<SDL_Rect>& damage = damageByTarget[toIndex(target)];
    // Join each region that overlaps, thus a pixel composes one time
    SDL_Rect joined = rect;
    bool grew       = true;
    while (grew) {
        grew = false;
        for (size_t i = 0; i < damage.size(); i++) {
            if (SDL_HasIntersection(&damage[i], &joined)) {
                SDL_UnionRect(&damage[i], &joined, &joined);
                damage.erase(damage.begin() + static_cast<long>(i));
                grew = true;
                break;
            }
        }
    }
    damage.push_back(joined);
}

void UiCompositor::drawFrame() {
    surfaceChanged = false;
    layersChanged  = false;
    if (!screen || !root) return;

    entries.clear();
    for (std::vector<SDL_Rect>& damage : damageByTarget) damage.clear();
    collectEntries(*root, true);

    if (clearLayersPending) clearUsedLayers();
    if (composeAll) {
        addDamage(UiTargetLayer::MainSurface, SDL_Rect{0, 0, screen->w, screen->h});
        composeAll = false;
    }

    // The first pass picks the views to draw. The draw can change the content rectangle, thus
    // the damage comes after it.
    for (Entry& entry : entries) {
        if (!entry.shown) continue;
        const auto bitmap = bitmaps.find(entry.viewId);
        entry.needsDraw   = entry.view->isDirty() || bitmap == bitmaps.end() || !bitmap->second.drawn ||
                          bitmap->second.drawnSize != UiSize{entry.screenRect.w, entry.screenRect.h};
    }
    drawViews();
    addEntryDamage();
    for (const TargetInfo& info : kTargets) {
        for (const SDL_Rect& region : damageByTarget[toIndex(info.target)]) compose(info.target, region);
    }

    lastShown.clear();
    std::unordered_set<int> laidOutIds;
    for (const Entry& entry : entries) {
        laidOutIds.insert(entry.viewId);
        if (entry.shown) lastShown[entry.viewId] = ShownEntry{entry.target, entry.screenRect, entry.visibleRect};
    }
    // A view that the layout no longer reaches loses its bitmap. A hidden view keeps it.
    for (auto it = bitmaps.begin(); it != bitmaps.end();) {
        if (laidOutIds.contains(it->first)) {
            ++it;
        } else {
            SDL_FreeSurface(it->second.surface);
            it = bitmaps.erase(it);
        }
    }
}

void UiCompositor::drawViews() {
    for (const Entry& entry : entries) {
        if (!entry.needsDraw) continue;
        Bitmap& bitmap    = bitmaps[entry.viewId];
        const UiSize size = {entry.screenRect.w, entry.screenRect.h};
        BitmapDrawContext context(bitmap.surface, size);
        entry.view->draw(context);
        SDL_Surface* drawn = context.getBitmap();
        if (bitmap.surface && bitmap.surface != drawn) SDL_FreeSurface(bitmap.surface);
        bitmap.surface     = drawn;
        bitmap.contentRect = context.getContentRect();
        bitmap.drawnSize   = size;
        bitmap.drawn       = true;
    }
}

void UiCompositor::addEntryDamage() {
    std::unordered_set<int> shownIds;
    for (Entry& entry : entries) {
        if (!entry.shown) continue;
        const auto bitmap = bitmaps.find(entry.viewId);
        if (bitmap == bitmaps.end() || !bitmap->second.surface) {
            entry.shown = false;
            continue;
        }
        const SDL_Rect& content = bitmap->second.contentRect;
        const SDL_Rect onScreen = {entry.screenRect.x + content.x, entry.screenRect.y + content.y, content.w,
                                   content.h};
        entry.visibleRect       = intersect(intersect(onScreen, entry.screenRect), entry.clipRect);
        if (isEmpty(entry.visibleRect)) {
            entry.shown = false;
            continue;
        }
        shownIds.insert(entry.viewId);

        const auto last = lastShown.find(entry.viewId);
        if (last == lastShown.end()) {
            addDamage(entry.target, entry.visibleRect);
        } else if (last->second.target != entry.target || !isSameRect(last->second.visibleRect, entry.visibleRect) ||
                   !isSameRect(last->second.screenRect, entry.screenRect)) {
            // A view that moves under a clip that does not move shows other pixels in the same place
            addDamage(last->second.target, last->second.visibleRect);
            addDamage(entry.target, entry.visibleRect);
        } else if (entry.needsDraw) {
            addDamage(entry.target, entry.visibleRect);
        }
    }
    for (const auto& [viewId, last] : lastShown) {
        if (!shownIds.contains(viewId)) addDamage(last.target, last.visibleRect);
    }
}

// The blend of a premultiplied source over a premultiplied destination
static uint32_t blendOver(uint32_t src, uint32_t dst) {
    const uint32_t alpha = src >> 24;
    if (alpha == 255) return src;
    if (alpha == 0) return dst;
    const uint32_t keep = 255 - alpha;
    uint32_t result     = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const uint32_t s = (src >> shift) & 0xFF;
        const uint32_t d = (dst >> shift) & 0xFF;
        uint32_t channel = s + (d * keep + 127) / 255;
        if (channel > 255) channel = 255;
        result |= channel << shift;
    }
    return result;
}

namespace {

uint32_t toStraightAlpha(uint32_t pixel) {
    const uint32_t alpha = pixel >> 24;
    if (alpha == 255 || alpha == 0) return alpha == 0 ? 0 : pixel;
    uint32_t result = alpha << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        uint32_t channel = (((pixel >> shift) & 0xFF) * 255 + alpha / 2) / alpha;
        if (channel > 255) channel = 255;
        result |= channel << shift;
    }
    return result;
}

void convertToStraightAlpha(SDL_Surface* surface) {
    for (int y = 0; y < surface->h; y++) {
        uint32_t* row = pixelAt(surface, 0, y);
        for (int x = 0; x < surface->w; x++) row[x] = toStraightAlpha(row[x]);
    }
}

}  // namespace

void UiCompositor::compose(UiTargetLayer target, const SDL_Rect& damaged) {
    const SDL_Rect region = intersect(damaged, SDL_Rect{0, 0, screen->w, screen->h});
    if (isEmpty(region)) return;

    SDL_Surface* buffer = SDL_CreateRGBSurfaceWithFormat(0, region.w, region.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!buffer) return;
    SDL_FillRect(buffer, nullptr, 0);

    for (const Entry& entry : entries) {
        if (!entry.shown || entry.target != target) continue;
        const auto bitmap = bitmaps.find(entry.viewId);
        if (bitmap == bitmaps.end()) continue;
        const SDL_Rect area = intersect(region, entry.visibleRect);
        if (isEmpty(area)) continue;
        SDL_Surface* source = bitmap->second.surface;
        for (int y = 0; y < area.h; y++) {
            const uint32_t* from = pixelAt(source, area.x - entry.screenRect.x, area.y - entry.screenRect.y + y);
            uint32_t* to         = pixelAt(buffer, area.x - region.x, area.y - region.y + y);
            for (int x = 0; x < area.w; x++) to[x] = blendOver(from[x], to[x]);
        }
    }

    // The buffer is premultiplied. A replace shows an opaque pixel the same in each form.
    const TargetInfo& info = getTargetInfo(target);
    if (info.blend == TargetBlend::Straight) convertToStraightAlpha(buffer);

    if (info.layer) {
        UiLayer_blit(buffer, region.x, region.y, *info.layer);
        usedTargets[toIndex(target)] = true;
        layersChanged                = true;
    } else {
        for (int y = 0; y < region.h; y++) {
            std::memcpy(pixelAt(screen, region.x, region.y + y), pixelAt(buffer, 0, y),
                        static_cast<size_t>(region.w) * sizeof(uint32_t));
        }
        surfaceChanged = true;
    }
    SDL_FreeSurface(buffer);
}

void UiCompositor::dropBitmaps() {
    for (auto& [viewId, bitmap] : bitmaps) SDL_FreeSurface(bitmap.surface);
    bitmaps.clear();
}
