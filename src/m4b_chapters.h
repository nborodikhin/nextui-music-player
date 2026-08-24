#ifndef __M4B_CHAPTERS_H__
#define __M4B_CHAPTERS_H__

#include <stdbool.h>
#include "audiobook.h"

// Standalone MP4 box walker for chapter data. minimp4 (src/audio/minimp4.h)
// demuxes audio tracks but never looks at `chpl` or QuickTime chapter tracks,
// so this reopens the file and parses them directly.

// Parse the chapters of an MP4/M4B file. Tries the Nero `chpl` atom first, then
// falls back to a QuickTime chapter track referenced via `tref`/`chap`.
// On success allocates *out (caller frees with free()) and returns the count.
// Returns 0 with *out == NULL when the file carries no chapter information.
//
// total_duration_ms is used to close out the final chapter; pass 0 if unknown.
int M4B_parseChapters(const char* path, int total_duration_ms, AudiobookChapter** out);

// Read `©nam` / `©ART` / `©alb` from the iTunes metadata atom (moov/udta/meta/ilst).
// Any out pointer may be NULL. Returns true if at least one tag was found.
bool M4B_readTags(const char* path, char* title, int title_size,
                  char* artist, int artist_size, char* album, int album_size);

#endif
