#ifndef __LYRICS_H__
#define __LYRICS_H__

#include <stdbool.h>

// A single timestamped lyric line
typedef struct {
    int time_ms;        // Timestamp in milliseconds
    char text[256];     // Lyric text
} LyricLine;

// Maximum number of lyric lines
#define LYRICS_MAX_LINES 512

// Initialize lyrics module
void Lyrics_init(void);

// Cleanup lyrics module
void Lyrics_cleanup(void);

// Fetch lyrics for artist/title (non-blocking, runs in background thread)
void Lyrics_fetch(const char* artist, const char* title, int duration_sec);

// Clear current lyrics and reset state
void Lyrics_clear(void);

// Returns the index of the current lyric at `position_ms`: the last one whose
// timestamp is not later than the position. Returns -1 before the first
// timestamp, and where no lyrics are loaded.
int Lyrics_currentIndex(int position_ms);

// Returns the count of loaded lines, and 0 where none are loaded.
int Lyrics_lineCount(void);

// Returns the text of the line at `index`, or NULL outside the loaded lines.
const char* Lyrics_lineText(int index);

// Check if lyrics are available for the current track
bool Lyrics_isAvailable(void);

#endif
