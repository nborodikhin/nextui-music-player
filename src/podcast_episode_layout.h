#ifndef __PODCAST_EPISODE_LAYOUT_H__
#define __PODCAST_EPISODE_LAYOUT_H__

// The geometry of the episode list: the feed summary and the episode rows are one
// stream of content that scrolls by the pixel through a viewport. This holds no
// font and no surface, thus it runs on the host. The renderer measures the
// summary and draws each part at the position that this gives.

typedef struct {
    int first_row_y;   // the top of episode 0, in content pixels
    int content_h;     // the summary, the gap and each row
    int scroll;        // the content pixel at the top of the viewport
} PodcastEpisodeLayout;

// Computes the layout of a feed whose summary is `summary_h` high, with `count`
// rows of `row_h` each, `gap` between the summary and the first row, in a
// viewport `viewport_h` high. Pass the row of the cursor in `selected` and the
// scroll of the last frame in `scroll`: the scroll moves only as far as the
// selected row needs to be visible, and it stops at the ends of the content.
// The cursor on row 0 shows the summary where the row fits under it.
PodcastEpisodeLayout PodcastEpisodeLayout_compute(int summary_h, int gap, int viewport_h,
                                                  int row_h, int count, int selected,
                                                  int scroll);

#endif
