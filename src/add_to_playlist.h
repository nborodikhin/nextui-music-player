#ifndef __ADD_TO_PLAYLIST_H__
#define __ADD_TO_PLAYLIST_H__

#include <stdbool.h>
#include <SDL2/SDL.h>

// Open the add-to-playlist dialog for a single indexed file
void AddToPlaylist_open(int file_id);

// Open the add-to-playlist dialog for all indexed audio files in a directory.
void AddToPlaylist_openDir(int dir_id);

// Check if the dialog is currently active
bool AddToPlaylist_isActive(void);

// Handle input for the dialog.
// Returns: 0 = still active, 1 = done (added or cancelled)
int AddToPlaylist_handleInput(void);

// Render the dialog overlay
void AddToPlaylist_render(SDL_Surface* screen);

#endif
