#ifndef __RESUME_H__
#define __RESUME_H__

#include <stdbool.h>

// Resume source types
typedef enum {
    RESUME_TYPE_NONE,
    RESUME_TYPE_FILES,
    RESUME_TYPE_PLAYLIST
} ResumeType;

// Resume state
typedef struct {
    ResumeType  type;
    int         dir_id;
    int         playlist_id;
    int         file_id;
    int         position_ms;
    char        track_name[256];
} ResumeState;

// Initialize (loads from disk if available)
void Resume_init(void);

// Check if resume state is available
bool Resume_isAvailable(void);

// Get current resume state (read-only)
const ResumeState* Resume_getState(void);

// Get display label for menu (e.g. "Resume: Song Name")
const char* Resume_getLabel(void);

// Save resume state for files playback
void Resume_saveFiles(int dir_id, int file_id, const char* track_name, int position_ms);

// Save resume state for playlist playback
void Resume_savePlaylist(int playlist_id, int file_id, const char* track_name, int position_ms);

// Update just the position (called periodically during playback)
void Resume_updatePosition(int position_ms);

// Clear resume state (when playlist ends naturally)
void Resume_clear(void);

#endif
