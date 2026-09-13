#include "lyric_window.h"

int LyricWindow_currentIndex(const LyricLine* lines, int count, int position_ms) {
    int lo = 0, hi = count - 1;
    int result = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lines[mid].time_ms <= position_ms) {
            result = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return result;
}

LyricWindow LyricWindow_layout(int line_count, int current, int rows) {
    LyricWindow window = {
        .first       = 0,
        .count       = 0,
        .current_row = -1,
    };
    if (line_count <= 0 || rows <= 0) return window;

    window.count = rows < line_count ? rows : line_count;
    if (current < 0) return window;
    if (current >= line_count) current = line_count - 1;

    int middle = (window.count - 1) / 2;
    int first = current - middle;
    int last_first = line_count - window.count;
    if (first > last_first) first = last_first;
    if (first < 0) first = 0;

    window.first       = first;
    window.current_row = current - first;
    return window;
}
