#ifndef __AUDIOBOOK_H__
#define __AUDIOBOOK_H__

#include <stdbool.h>
#include <stdint.h>

// A "book" is either a subdirectory of /Audiobook (its audio files, sorted by
// name, are the chapters) or a single file at the top level (chapters come from
// the .m4b container, or one chapter covering the whole file).

#define AUDIOBOOK_MAX_TITLE   256
#define AUDIOBOOK_MAX_AUTHOR  128
#define AUDIOBOOK_MAX_PATH    512

typedef struct {
    char title[AUDIOBOOK_MAX_TITLE];
    char author[AUDIOBOOK_MAX_AUTHOR];
    char path[AUDIOBOOK_MAX_PATH];   // Directory, or the single file
    char key[AUDIOBOOK_MAX_PATH];    // Path relative to /Audiobook — the progress key
    bool single_file;
    int  chapter_count;
    int  total_duration_ms;

    // Progress
    int  current_chapter;
    int  position_ms;                // Position within current_chapter
    bool finished;
    uint32_t last_played;            // Unix time, 0 = never started
} Audiobook;

typedef struct {
    char title[AUDIOBOOK_MAX_TITLE];
    char path[AUDIOBOOK_MAX_PATH];   // == book->path for single-file books
    int  start_ms;                   // Offset within path (0 for multi-file books)
    int  duration_ms;
} AudiobookChapter;

// Root of the audiobook library on the SD card
const char* Audiobook_getRootPath(void);

// Create data directories and load persisted progress. Safe to call repeatedly.
int Audiobook_init(void);

// Flush progress and release the in-memory library
void Audiobook_cleanup(void);

// (Re)scan /Audiobook. Metadata is cached per book and only recomputed when the
// book's mtime changes, so repeated scans are cheap.
void Audiobook_scanLibrary(void);

int Audiobook_getCount(void);
Audiobook* Audiobook_get(int index);

// Index of the most recently played book that is not finished, or -1 when no
// book is in progress. Backs the pinned "Continue" row of the library screen.
int Audiobook_getResumeIndex(void);

// Load the chapter list of a book into the shared chapter buffer.
// Returns the chapter count; previous chapter data is discarded.
int Audiobook_loadChapters(const Audiobook* book);
int Audiobook_getChapterCount(void);
AudiobookChapter* Audiobook_getChapter(int index);

// Start time of a chapter measured from the beginning of the whole book
int Audiobook_getChapterBookOffsetMs(int chapter_index);

// Record progress in memory (call often); flush writes it to disk (call rarely)
void Audiobook_saveProgress(Audiobook* book, int chapter, int position_ms);
void Audiobook_markFinished(Audiobook* book, bool finished);
void Audiobook_flushProgress(void);

#endif
