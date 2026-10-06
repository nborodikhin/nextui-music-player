#include "resume.h"

#include <stdio.h>
#include <string.h>

#include "db.h"
#include "filedb.h"
#include "platform.h"

// In-memory state
static ResumeState state = { .type = RESUME_TYPE_NONE };
static char label_buf[300];

void Resume_init(void) {
    memset(&state, 0, sizeof(state));
    state.type = RESUME_TYPE_NONE;

    DbLastPlayedResult* last_played_result = Db_readLastPlayed();
    if (!last_played_result) return;

    const DbLastPlayed last_played = last_played_result->last_played;
    Db_freeResult(last_played_result);

    DbFileResult* file_result = Db_getFile(last_played.id2);
    if (file_result && FileDb_fileExists(last_played.id2)) {
        if (last_played.type == DB_LAST_PLAYED_PLAYLIST) {
            state.type = RESUME_TYPE_PLAYLIST;
            state.dir_id = 0;
            state.playlist_id = last_played.id1;
        } else if (last_played.type == DB_LAST_PLAYED_FOLDER) {
            state.type = RESUME_TYPE_FILES;
            state.dir_id = last_played.id1;
            state.playlist_id = 0;
        }
        state.file_id = last_played.id2;
        state.position_ms = last_played.position;
        if (last_played.track_name[0]) {
            snprintf(state.track_name, sizeof(state.track_name), "%s", last_played.track_name);
        } else {
            snprintf(state.track_name, sizeof(state.track_name), "%s", file_result->file.filename);
            char* extension = strrchr(state.track_name, '.');
            if (extension && extension != state.track_name) *extension = '\0';
        }
    }
    Db_freeResult(file_result);
}

bool Resume_isAvailable(void) {
    return state.type != RESUME_TYPE_NONE;
}

const ResumeState* Resume_getState(void) {
    if (state.type == RESUME_TYPE_NONE) return NULL;
    return &state;
}

const char* Resume_getLabel(void) {
    if (state.type == RESUME_TYPE_NONE) return NULL;
    snprintf(label_buf, sizeof(label_buf), "Resume: %s", state.track_name);
    return label_buf;
}

void Resume_saveFiles(int dir_id, int file_id, const char* track_name, int position_ms) {
    memset(&state, 0, sizeof(state));
    state.type = RESUME_TYPE_FILES;
    state.dir_id = dir_id;
    state.playlist_id = 0;
    state.file_id = file_id;
    state.position_ms = position_ms;
    snprintf(state.track_name, sizeof(state.track_name), "%s", track_name ? track_name : "");
    Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, dir_id, file_id, track_name,
                      position_ms);
}

void Resume_savePlaylist(int playlist_id, int file_id, const char* track_name,
                         int position_ms) {
    memset(&state, 0, sizeof(state));
    state.type = RESUME_TYPE_PLAYLIST;
    state.dir_id = 0;
    state.playlist_id = playlist_id;
    state.file_id = file_id;
    state.position_ms = position_ms;
    snprintf(state.track_name, sizeof(state.track_name), "%s", track_name ? track_name : "");
    Db_saveLastPlayed(DB_LAST_PLAYED_PLAYLIST, playlist_id, file_id, track_name,
                      position_ms);
}

void Resume_updatePosition(int position_ms) {
    if (state.type == RESUME_TYPE_NONE) return;
    state.position_ms = position_ms;
    Db_saveLastPlayedPosition(position_ms);
}

void Resume_clear(void) {
    memset(&state, 0, sizeof(state));
    Db_clearLastPlayed();
}
