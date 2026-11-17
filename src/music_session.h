#ifndef __MUSIC_SESSION_H__
#define __MUSIC_SESSION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "playlist.h"
#include "resume.h"

// The music playback session: the queue, its source, shuffle and repeat, the step to the next
// track and the music record. It has no screen. Each screen reads its state and commands it.

// Plays the queue of the folder `dir_id` from the file `start_file_id`. Pass 0 in
// `start_file_id` to start at the first file. When the app cannot build the queue of the
// folder, the queue holds the start file only.
// Returns: true when a track plays
bool MusicSession_startFolder(int dir_id, int start_file_id);

// Plays a copy of `tracks` from `start_index`. The caller keeps `tracks`.
// Returns: true when a track plays
bool MusicSession_startPlaylist(const PlaylistTrack* tracks, int track_count, int start_index,
                                int playlist_id);

// Plays the source of `resume`, and seeks the saved file to the saved position.
// Returns: true when a track plays
bool MusicSession_startResume(const ResumeState* resume);

// Starts the next track when a track ends, ends the queue, and saves the position at an
// interval. Draws nothing and reads no input.
void MusicSession_tick(void);

// Plays the next track of the queue, and the first one after the last one
void MusicSession_next(void);

// Plays the previous track of the queue, and the last one before the first one
void MusicSession_prev(void);

// Saves the position, stops the playback and frees the queue. Keeps the music record.
void MusicSession_stop(void);

// Writes the position of the track to the music record
void MusicSession_savePosition(void);

// Frees the queue at the exit of the app
void MusicSession_quit(void);

// Returns: true while the session has a queue: from the start of a queue to its stop or its end
bool MusicSession_isActive(void);

// Returns a number that changes each time a track starts
unsigned MusicSession_getTrackSerial(void);

// Returns the 1-based position of the current track in the queue
int MusicSession_getTrackNumber(void);

int MusicSession_getTrackCount(void);

bool MusicSession_isShuffle(void);
bool MusicSession_isRepeat(void);
void MusicSession_toggleShuffle(void);
void MusicSession_toggleRepeat(void);

#ifdef __cplusplus
}
#endif

#endif
