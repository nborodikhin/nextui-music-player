#ifndef __PLAYLIST_M3U_H__
#define __PLAYLIST_M3U_H__

#include <stdbool.h>
#include <stddef.h>
#include "playlist.h"  // For PlaylistTrack

#define MAX_PLAYLISTS  50
#define MAX_PLAYLIST_NAME 128

typedef struct {
    int id;
    char name[MAX_PLAYLIST_NAME];  // Display name (without .m3u)
    char path[512];                // Full path to .m3u file
    int track_count;               // Number of tracks (from quick scan)
} PlaylistInfo;

// Create playlists directory if it doesn't exist
void M3U_init(void);

// Read indexed playlists into an array. Returns count.
int M3U_listPlaylists(PlaylistInfo* out, int max);

// Create an empty .m3u file with the given name. Returns the id of the playlist, or 0 on error.
int M3U_create(const char* name);

// Returns the info of a playlist, or NULL when the playlist has no row. The caller frees it.
PlaylistInfo* M3U_getInfo(int playlist_id);

// Delete the file and the row of a playlist. Returns false on error.
bool M3U_delete(int playlist_id);

// Appends files to the .m3u file of a playlist. The file then holds only entries that resolve
// to indexed music files, thus an entry of a file that is gone goes. A file that is not music,
// or that the playlist holds already, is not added. Returns the number of files added.
int M3U_addTracks(int playlist_id, const int* file_ids, int file_count);

// Rewrite the .m3u file of a playlist without the entries of a file. The file then holds only
// entries that resolve to indexed music files. Returns false when the playlist has no row, held
// no entry of the file, or the write failed.
bool M3U_removeTrack(int playlist_id, int file_id);

// Load the tracks of a playlist into a PlaylistTrack array, at most max. Skips entries that do
// not resolve to indexed music files. Returns the count of tracks, or -1 when the playlist has
// no row or its file cannot be read.
int M3U_loadTracks(int playlist_id, PlaylistTrack* tracks, int max);

#endif
