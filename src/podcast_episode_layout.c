#include "podcast_episode_layout.h"

PodcastEpisodeLayout PodcastEpisodeLayout_compute(int summary_h, int gap, int viewport_h,
                                                  int row_h, int count, int selected,
                                                  int scroll) {
    PodcastEpisodeLayout layout;
    layout.first_row_y = summary_h + gap;
    layout.content_h   = count > 0 ? layout.first_row_y + count * row_h : summary_h;

    if (count > 0 && selected >= 0 && selected < count) {
        int sel_y      = layout.first_row_y + selected * row_h;
        int sel_bottom = sel_y + row_h;

        if (selected == 0) {
            // The summary shows with the first row, where the two fit together.
            scroll = 0;
        }
        if (sel_bottom - scroll > viewport_h) scroll = sel_bottom - viewport_h;
        if (sel_y < scroll) scroll = sel_y;
    }

    int max_scroll = layout.content_h - viewport_h;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;
    if (scroll < 0) scroll = 0;

    layout.scroll = scroll;
    return layout;
}
