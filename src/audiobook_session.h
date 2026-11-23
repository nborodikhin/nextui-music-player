#ifndef __AUDIOBOOK_SESSION_H__
#define __AUDIOBOOK_SESSION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// The C side of the audiobook session (audiobook/session.hpp), for the background slot.

// Returns true while a book is open: playing, paused, or seeking to its saved place
bool AudiobookSession_isActive(void);

// Moves to the next chapter, ends the book, runs the sleep timer and saves the place
void AudiobookSession_tick(void);

// Saves the place of the book, and stops it
void AudiobookSession_stop(void);

// Writes the progress and the cache of the library at the exit of the app
void AudiobookSession_quit(void);

#ifdef __cplusplus
}
#endif

#endif
