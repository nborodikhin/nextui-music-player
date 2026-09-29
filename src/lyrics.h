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

// Fetch lyrics for a track (non-blocking, runs in background thread). The first
// source that has lyrics wins: the .lrc file next to `track_path`, the
// `embedded` text from the tags of the file, the disk cache, then LRCLIB.
// Text with timestamps is read as LRC, other text as plain lines with estimated
// times. Each argument may be NULL or empty; the strings are copied. A track
// without artist and title uses only the lyrics of its file.
void Lyrics_fetch(const char* artist, const char* title, int duration_sec,
                  const char* track_path, const char* embedded);

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

// True where the loaded lyrics have real timestamps (LRC). False for plain text,
// whose times are estimates, and where no lyrics are loaded.
bool Lyrics_isSynced(void);

#endif
