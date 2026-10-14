#ifndef __FILEDB_H__
#define __FILEDB_H__

#include <stdbool.h>

void FileDb_start(void);
void FileDb_quit(void);
bool FileDb_isIdle(void);

// schedule an action and return the request id (positive)
int FileDb_scanAll(void);
int FileDb_scanDir(int dir_id, bool recursive);
int FileDb_scanFile(int file_id);
// Checks the .m3u file of a playlist and its entry count, and lists the directory of each
// entry, thus a lookup of an entry finds the current row of its file.
int FileDb_scanPlaylist(int playlist_id);
// Makes the playlist rows match the .m3u files on the card.
int FileDb_scanPlaylists(void);

// returns true if request_id is <= 0 or request has completed
bool FileDb_isRequestDone(int request_id);
// returns true while the worker thread runs; a request that is not done
// when the worker is not running does not complete until the next start
bool FileDb_isRunning(void);
// waits for a positive request id; returns false for a non-positive id and
// when the worker stops, or cannot start, before the request is done
bool FileDb_waitBlocking(int request_id);

// Returns true when the file has an index row and is a regular file on the card. Reads the card
// on the thread of the caller, and does not change the index.
bool FileDb_fileExists(int file_id);

#endif
