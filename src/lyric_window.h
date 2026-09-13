#ifndef __LYRIC_WINDOW_H__
#define __LYRIC_WINDOW_H__

#include "lyrics.h"

// The timing and the layout of the lyric window. Pure logic: the lyric model
// holds the lines, and the music playing screen draws the rows.

// Returns the index of the current lyric: the last one whose timestamp is not
// later than `position_ms`. Returns -1 before the first timestamp, and where
// there are no lines. The lines come in the order of their timestamps.
int LyricWindow_currentIndex(const LyricLine* lines, int count, int position_ms);

typedef struct {
    int first;        // index of the lyric on the top row
    int count;        // rows that hold a lyric
    int current_row;  // row of the current lyric, or -1 where none is current
} LyricWindow;

// Lays out `rows` rows over `line_count` lyrics around `current`. The current
// lyric takes the middle row, and the upper middle row where `rows` is even.
// The window clamps to the first lyric near the start and to the last lyric
// near the end. Pass -1 in `current` before the first timestamp: the window
// starts at the first lyric and no row is current.
LyricWindow LyricWindow_layout(int line_count, int current, int rows);

#endif
