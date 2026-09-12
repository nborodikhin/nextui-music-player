#ifndef __SCREEN_TITLE_H__
#define __SCREEN_TITLE_H__

#include <stdbool.h>
#include <stdint.h>

// The state of a screen title: a tape of texts that moves through the title
// area, and the widths that the renderer measured. It holds no font and no
// surface, thus it runs on the host. The header renderer measures each text
// and paints the slice of the tape that the offset gives.
//
// A change of text while the tape moves never jumps. The new text goes on the
// tape after the segment under the right edge of the window: that segment goes
// on for one fade past the edge, where it washes out to transparent, or it is
// padded to the edge where its text ended already. What is in the window stays
// as it is, and the new text scrolls in.

// As long as a file-browser path.
#define SCREEN_TITLE_MAX 512

// How many texts the tape holds. A text that is not in the window yet can be
// replaced, thus the tape needs one more than the texts a window can show.
#define SCREEN_TITLE_SEGMENTS 6

// How long a new title stays still before it moves.
#define SCREEN_TITLE_START_DELAY_MS 1000

// The speed of the marquee, in pixels for each second. Two pixels a frame at 60
// frames a second, which is the speed of the software marquee of a row.
#define SCREEN_TITLE_SPEED_PX_PER_S 120

typedef struct {
    char text[SCREEN_TITLE_MAX];
    int  text_w;   // the width of the whole text
    int  len;      // the room on the tape: the text, or the text cut or padded
    bool cut;      // the text washes out over the last `fade_w` of `len`
} ScreenTitleSegment;

typedef struct {
    ScreenTitleSegment seg[SCREEN_TITLE_SEGMENTS];
    int      count;       // seg[0] is the text on the screen
    bool     defer;       // a change while the tape moves scrolls in, and never jumps
    uint32_t shown_at;    // when the tape started its start delay
    int      dropped_px;  // the room of the texts that scrolled out since then
    int      area_w;      // the width of the title area on the last frame
    int      gap;         // the space between two texts on the tape
    int      fade_w;      // the width of the wash at a cut, one em
} ScreenTitle;

// Pass true in `defer` to let a change scroll in while the tape moves. A title
// with `defer` false replaces its text at once.
void ScreenTitle_reset(ScreenTitle* title, bool defer);

// Returns true where `text` is the text at the end of the tape, thus a set of
// it changes nothing. The renderer asks before it measures the text.
bool ScreenTitle_isCurrent(const ScreenTitle* title, const char* text);

// Gives the title its text, and its width in `text_w` as the renderer measured
// it. A text that differs from the last one on the tape replaces the tape at
// once where the tape is at rest, and goes on the tape where it moves and
// `defer` is set. Returns true where the text on the screen changed.
bool ScreenTitle_set(ScreenTitle* title, const char* text, int text_w, uint32_t now);

// Records the widths that the renderer measured on this frame. A title at rest
// whose area became too narrow starts its marquee from the start, at `now`.
void ScreenTitle_measure(ScreenTitle* title, int area_w, int gap, int fade_w, uint32_t now);

// Returns true where the tape is wider than the area and the start delay is over.
bool ScreenTitle_moves(const ScreenTitle* title, uint32_t now);

// Returns the offset of the window from the start of seg[0], in pixels. Drops
// each text that scrolled out, thus seg[0] is the text at the offset. A title
// at rest returns 0.
int ScreenTitle_offset(ScreenTitle* title, uint32_t now);

// Returns true where the title needs a frame of the surface. Pass true in
// `shown` where the surface shows the title at rest: a title that moves must
// leave the surface, and a title that stopped must go on it. The marquee itself
// is on a GPU layer, thus it asks for no other frame.
bool ScreenTitle_needsFrame(const ScreenTitle* title, uint32_t now, bool shown);

#endif
