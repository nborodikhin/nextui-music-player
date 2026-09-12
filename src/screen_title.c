#include <stdio.h>
#include <string.h>

#include "screen_title.h"

void ScreenTitle_reset(ScreenTitle* title, bool defer) {
    memset(title, 0, sizeof(*title));
    title->defer = defer;
}

static void put(ScreenTitleSegment* seg, const char* text, int text_w) {
    snprintf(seg->text, sizeof(seg->text), "%s", text);
    seg->text_w = text_w;
    seg->len    = text_w;
    seg->cut    = false;
}

// The tape is wider than the window where the text does not fit, or where
// another text follows it.
static bool tape_is_wide(const ScreenTitle* title) {
    return title->count > 1 || (title->count == 1 && title->seg[0].text_w > title->area_w);
}

// The distance that the tape moved since its start delay ended.
static int distance_moved(const ScreenTitle* title, uint32_t now) {
    uint32_t elapsed = now - title->shown_at - SCREEN_TITLE_START_DELAY_MS;
    return (int)((uint64_t)elapsed * SCREEN_TITLE_SPEED_PX_PER_S / 1000);
}

bool ScreenTitle_moves(const ScreenTitle* title, uint32_t now) {
    return tape_is_wide(title) && now - title->shown_at >= SCREEN_TITLE_START_DELAY_MS;
}

int ScreenTitle_offset(ScreenTitle* title, uint32_t now) {
    if (!ScreenTitle_moves(title, now)) return 0;

    int distance = distance_moved(title, now) - title->dropped_px;
    while (title->count > 1 && distance >= title->seg[0].len + title->gap) {
        // The head left the window, and the next text keeps the movement. The
        // room that left counts in pixels, thus the clock keeps no rounding.
        int gone = title->seg[0].len + title->gap;
        distance -= gone;
        title->dropped_px += gone;
        memmove(&title->seg[0], &title->seg[1], (title->count - 1) * sizeof(title->seg[0]));
        title->count--;
    }

    if (title->count == 1) {
        if (title->seg[0].text_w <= title->area_w) {
            // The last text fits, and it reached its place: it is at rest
            title->shown_at   = now;
            title->dropped_px = 0;
            return 0;
        }
        // The one text loops. Each loop that ended counts as room that left the
        // tape, thus a change finds the offset inside the current loop.
        int period = title->seg[0].len + title->gap;
        int loops  = distance / period;
        title->dropped_px += loops * period;
        distance -= loops * period;
    }
    return distance;
}

bool ScreenTitle_isCurrent(const ScreenTitle* title, const char* text) {
    if (!text) text = "";
    return title->count > 0 && strcmp(title->seg[title->count - 1].text, text) == 0;
}

bool ScreenTitle_set(ScreenTitle* title, const char* text, int text_w, uint32_t now) {
    if (!text) text = "";

    if (ScreenTitle_isCurrent(title, text)) return false;

    if (!title->defer || title->count == 0 || !ScreenTitle_moves(title, now)) {
        // At rest: the new text takes the screen at once
        put(&title->seg[0], text, text_w);
        title->count      = 1;
        title->shown_at   = now;
        title->dropped_px = 0;
        return true;
    }

    // The tape moves. Find the segment under the right edge of the window, cut
    // or pad it there, and put the new text after it. What the window shows
    // does not change.
    int offset = ScreenTitle_offset(title, now);
    int edge   = offset + title->area_w;
    int pos    = 0;
    int i      = 0;
    // The window is [offset, edge): a text that starts at the edge is not in it
    while (i < title->count - 1 && edge > pos + title->seg[i].len + title->gap) {
        pos += title->seg[i].len + title->gap;
        i++;
    }
    ScreenTitleSegment* under = &title->seg[i];
    int in_seg = edge - pos;
    if (in_seg < under->len) {
        // The edge is inside the text, or inside the room that it took already
        if (in_seg < under->text_w) {
            under->len = in_seg + title->fade_w;
            under->cut = true;
        }
    } else if (in_seg > under->len + title->gap) {
        if (title->count == 1) {
            // The edge is in the next copy of a text that loops: that copy is
            // on the screen, thus it goes on the tape, cut at the edge
            int in_copy = in_seg - (under->len + title->gap);
            ScreenTitleSegment* copy = &title->seg[1];
            put(copy, under->text, under->text_w);
            copy->len = in_copy + title->fade_w;
            copy->cut = true;
            title->count = 2;
            i = 1;
        } else {
            // Past the last text and its gap: the text takes the room up to
            // the edge, thus the new one comes in after what the window shows
            under->len = in_seg;
            under->cut = false;
        }
    }
    // Else the edge is in the gap after the text: the text keeps its room

    int next = i + 1;
    if (next >= SCREEN_TITLE_SEGMENTS) {
        // The tape is full of texts that the window shows. The oldest one after
        // the head leaves, thus the text under the edge stays as it is.
        memmove(&title->seg[1], &title->seg[2], (SCREEN_TITLE_SEGMENTS - 2) * sizeof(title->seg[0]));
        next = SCREEN_TITLE_SEGMENTS - 1;
    }
    put(&title->seg[next], text, text_w);
    title->count = next + 1;
    return false;
}

void ScreenTitle_measure(ScreenTitle* title, int area_w, int gap, int fade_w, uint32_t now) {
    bool was_wide = tape_is_wide(title);
    title->area_w = area_w;
    title->gap    = gap;
    title->fade_w = fade_w;
    if (!was_wide && tape_is_wide(title)) {
        // A narrower area, such as one with a setting pill, starts the marquee
        // of a text at rest: from the start and after the delay, not from
        // where the old clock says
        title->shown_at   = now;
        title->dropped_px = 0;
    }
}

bool ScreenTitle_needsFrame(const ScreenTitle* title, uint32_t now, bool shown) {
    // A title at rest belongs on the surface, a title that moves does not.
    return ScreenTitle_moves(title, now) == shown;
}
