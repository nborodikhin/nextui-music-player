#include "ui/view/title_text_view.hpp"

#include <algorithm>
#include <cstdint>

#include "ui_utils.h"

// The NextUI headers declare C functions and have no guard of their own
extern "C" {
#include "defines.h"
#include "api.h"
}

// Washes out the pixels of `bitmap` from `end` - `fadeWidth` to `end`, from opaque to transparent.
// The pixels are premultiplied, thus each channel takes the same factor.
static void washOut(SDL_Surface* bitmap, int end, int fadeWidth) {
    end             = std::min(end, bitmap->w);
    const int start = std::max(0, end - fadeWidth);
    for (int y = 0; y < bitmap->h; y++) {
        uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(bitmap->pixels) + y * bitmap->pitch);
        for (int x = start; x < end; x++) {
            const uint32_t keep = static_cast<uint32_t>(255 * (end - x) / fadeWidth);
            uint32_t pixel      = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                pixel |= (((row[x] >> shift) & 0xFF) * keep / 255) << shift;
            }
            row[x] = pixel;
        }
    }
}

TitleTextView::~TitleTextView() { freeSegments(); }

void TitleTextView::finishDraw(SDL_Surface* bitmap) {
    if (isScrolling() || !isClipped()) return;
    // The text washes out over one em at the cut
    washOut(bitmap, bitmap->w, std::min(getFontHeight(), bitmap->w));
}

void TitleTextView::setFollowText(bool newFollow) {
    if (follow == newFollow) return;
    follow = newFollow;
    if (follow) {
        restartTape();
        if (hasContext() && !getScheduler().isTaskPending(followTask)) {
            followTask = getScheduler().runEachFrame([this] { stepFollow(); });
        }
    } else {
        getScheduler().cancelTask(followTask);
        followTask = 0;
        freeSegments();
    }
}

bool TitleTextView::setText(const std::string& text) {
    const bool changed = TextView::setText(text);
    if (!changed) return false;
    followText = text;
    if (!follow) return true;
    if (!hasContext()) {
        // The attach puts the text on the tape at the active time of the page
        return changed;
    }
    int textWidth = 0;
    TTF_SizeUTF8(getFont(), text.c_str(), &textWidth, nullptr);
    ScreenTitle_set(&tape, text.c_str(), textWidth, getNow());
    followDirty = true;
    return changed;
}

uint32_t TitleTextView::getNow() const { return hasContext() ? getContext().getActiveTime() : 0; }

void TitleTextView::restartTape() {
    ScreenTitle_reset(&tape, true, SCALE1(SCROLLING_SPEED_SP));
    followOffset = 0;
    followCount  = 0;
    followDirty  = true;
    if (followText.empty()) return;
    int textWidth = 0;
    TTF_SizeUTF8(getFont(), followText.c_str(), &textWidth, nullptr);
    ScreenTitle_set(&tape, followText.c_str(), textWidth, getNow());
}

void TitleTextView::onAttach(UiContext& context) {
    TextView::onAttach(context);
    if (!follow) return;
    // A new screen draws each text again, and the title starts at rest
    freeSegments();
    restartTape();
    followTask = getScheduler().runEachFrame([this] { stepFollow(); });
}

void TitleTextView::stepFollow() {
    const uint32_t now = getNow();
    const int offset   = ScreenTitle_moves(&tape, now) ? ScreenTitle_offset(&tape, now) : 0;
    if (offset == followOffset && tape.count == followCount) return;
    followOffset = offset;
    followCount  = tape.count;
    followDirty  = true;
}

void TitleTextView::layout(UiLayoutContext& context) {
    TextView::layout(context);
    if (!follow) return;
    const int areaWidth = context.getMaxSize().w;
    const int em        = getFontHeight();
    const uint32_t now  = getNow();
    // One em between two texts, and one em of wash at a cut
    ScreenTitle_measure(&tape, areaWidth, em, em, now);
    const bool moves = ScreenTitle_moves(&tape, now);
    if (moves != moving) {
        moving      = moves;
        followDirty = true;
    }
    const int restWidth = tape.count > 0 ? std::min(tape.seg[0].text_w, areaWidth) : 0;
    context.setSize(UiSize{moves ? areaWidth : restWidth, context.getSize().h});
    context.setTargetLayer(moves ? UiTargetLayer::AnimationLayer : UiTargetLayer::MainSurface);
}

bool TitleTextView::isDirty() const { return TextView::isDirty() || (follow && followDirty); }

void TitleTextView::freeSegments() {
    for (SegmentBitmap& segment : segments) {
        if (segment.surface) SDL_FreeSurface(segment.surface);
        segment = SegmentBitmap{};
    }
}

SDL_Surface* TitleTextView::getSegmentBitmap(int slot) {
    const ScreenTitleSegment& source = tape.seg[slot];
    SegmentBitmap& cached            = segments[static_cast<size_t>(slot)];
    const SDL_Color color            = getColor();
    const bool sameColor = cached.color.r == color.r && cached.color.g == color.g && cached.color.b == color.b
                           && cached.color.a == color.a;
    if (cached.surface && cached.len == source.len && cached.cut == source.cut && sameColor
        && cached.text == source.text) {
        return cached.surface;
    }
    if (cached.surface) SDL_FreeSurface(cached.surface);
    cached = SegmentBitmap{
        .text    = source.text,
        .len     = source.len,
        .cut     = source.cut,
        .color   = color,
        .surface = renderBitmap(source.text),
    };
    // A cut text washes out over the last em of its room, and ends there
    if (cached.surface && source.cut) washOut(cached.surface, source.len, getFontHeight());
    return cached.surface;
}

void TitleTextView::draw(UiDrawContext& context) {
    if (!follow) {
        TextView::draw(context);
        return;
    }
    clearDirty();
    followDirty         = false;
    SDL_Surface* target = context.getSurface();
    if (!target || tape.count == 0) return;
    context.setContentRect(SDL_Rect{0, getTextTop(), target->w, getFontHeight()});

    const int offset = moving ? ScreenTitle_offset(&tape, getNow()) : 0;
    followOffset     = offset;
    followCount      = tape.count;
    int position     = -offset;
    for (int slot = 0; slot < tape.count && position < target->w; slot++) {
        SDL_Surface* bitmap = getSegmentBitmap(slot);
        if (bitmap) {
            SDL_Rect from = {0, 0, tape.seg[slot].cut ? tape.seg[slot].len : bitmap->w, bitmap->h};
            SDL_Rect to   = {position, getTextTop(), 0, 0};
            SDL_BlitSurface(bitmap, &from, target, &to);
        }
        position += tape.seg[slot].len + tape.gap;
    }
    // The one text loops: its next copy follows the gap
    if (tape.count == 1 && offset > 0 && position < target->w) {
        SDL_Surface* bitmap = getSegmentBitmap(0);
        if (bitmap) {
            SDL_Rect to = {position, getTextTop(), 0, 0};
            SDL_BlitSurface(bitmap, nullptr, target, &to);
        }
    }
    // A long text at rest washes out at the edge
    if (!moving && tape.seg[0].text_w > target->w) {
        washOut(target, target->w, std::min(getFontHeight(), target->w));
    }
}
