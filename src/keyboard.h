#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#include <stddef.h>

// Longest text the keyboard takes, terminator included
#define KEYBOARD_MAX_INPUT 512

// Initialize keyboard module
// Call this before using Keyboard_open()
void Keyboard_init(void);

// Open the on-screen keyboard - blocking call until the user is done.
// Returned strings will contain full UTF-8 characters up to max_bytes long
// (not including the null terminator).
// If max_bytes is 0 or less, the default limit is used (512).
//
// Returns allocated string that caller must free, or NULL if cancelled.
//
// Note: keyboard runs its own frame loop, so the caller's frame is over when it
// returns. Callers must re-read the SDL_Surface from DisplayContext.
char* Keyboard_open(const char* prompt, size_t max_bytes);

#endif // __KEYBOARD_H__
