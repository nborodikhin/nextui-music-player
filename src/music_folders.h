#ifndef __MUSIC_FOLDERS_H__
#define __MUSIC_FOLDERS_H__

#include <stdbool.h>

// User-configurable list of folders that contain music.
// Persisted to <shared userdata>/music-player/folders.txt, one absolute path per line.
// The default "/Music" folder is always present and cannot be removed (downloads
// are written there). Folder order is preserved.

#define MUSIC_FOLDERS_MAX 32

// Load the folder list from disk. Ensures the pinned default folder is present.
// Safe to call once at startup.
void MusicFolders_init(void);

// Number of configured folders (>= 1).
int MusicFolders_count(void);

// Absolute path of folder i (0-based), or NULL if out of range.
const char* MusicFolders_get(int i);

// True if folder i exists on disk right now.
bool MusicFolders_existsAt(int i);

// True if folder i is the pinned default ("/Music") and cannot be removed.
bool MusicFolders_isPinned(int i);

// The pinned default folder path (SDCARD "/Music").
const char* MusicFolders_defaultPath(void);

// Add a folder. Returns true if added; false if rejected (duplicate, nested under
// or containing an existing folder, list full, or empty path). Saves on success.
bool MusicFolders_add(const char* path);

// Remove folder i. Refuses to remove the pinned folder. Returns true if removed.
// Saves on success.
bool MusicFolders_removeAt(int i);

#endif
