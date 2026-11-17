#include "music_session.h"

#include <stdio.h>
#include <string.h>

#include "album_art.h"
#include "background.h"
#include "file_utils.h"
#include "lyrics.h"
#include "module_common.h"
#include "player.h"
#include "playlist_m3u.h"
#include "settings.h"

// The interval of the position save while a track plays
#define POSITION_SAVE_MS 5000

static PlaylistContext queue = {0};
// The source of the queue: a playlist id, or 0 for the folder dir_id
static int source_dir_id = 0;
static int source_playlist_id = 0;
static bool shuffle_enabled = false;
static bool repeat_enabled = false;
static uint32_t last_position_save = 0;
static unsigned track_serial = 0;

// The seek of a Resume. It applies to the first start only, and only to the saved file.
typedef struct {
    int file_id;
    int position_ms;
} PendingSeek;

static void write_music_record(const PlaylistTrack* track) {
    const TrackInfo* info = Player_getTrackInfo();
    char track_name[256];
    if (info && info->title[0]) {
        snprintf(track_name, sizeof(track_name), "%s", info->title);
    } else {
        const char* slash = strrchr(track->path, '/');
        get_file_display_name(slash ? slash + 1 : track->path, track_name, sizeof(track_name));
    }
    int position_ms = Player_getPosition();
    if (source_playlist_id > 0) {
        Resume_savePlaylist(source_playlist_id, track->file_id, track_name, position_ms);
    } else {
        Resume_saveFiles(source_dir_id, track->file_id, track_name, position_ms);
    }
    last_position_save = SDL_GetTicks();
}

static bool start_track(const PlaylistTrack* track, void* user) {
    PendingSeek* seek = (PendingSeek*)user;
    int seek_ms = 0;
    if (seek) {
        if (seek->file_id == track->file_id) seek_ms = seek->position_ms;
        seek->position_ms = 0;
    }

    if (Player_load(track->path) != 0) return false;
    Player_play();
    const TrackInfo* info = Player_getTrackInfo();

    // The fetch of the album art runs in the background
    if (info && !Player_getAlbumArt() && (info->artist[0] || info->title[0])) {
        album_art_fetch(info->artist, info->title);
    }
    // The lyrics belong to the track, thus a new track takes the old ones away, and fetches
    // its own where the lyrics are on
    Lyrics_clear();
    if (Settings_getBool(&SETTING_LYRICS_ENABLED) && info) {
        Lyrics_fetch(info->artist, info->title, info->duration_ms / 1000,
                     Player_getCurrentFile(), Player_getEmbeddedLyrics());
    }

    if (seek_ms > 0) Player_seek(seek_ms);
    write_music_record(track);
    track_serial++;
    return true;
}

static PlaylistStep current_step(void) {
    if (repeat_enabled) return PLAYLIST_STEP_REPEAT;
    if (shuffle_enabled) return PLAYLIST_STEP_SHUFFLE;
    return PLAYLIST_STEP_NEXT;
}

// Pass true in `save` to write the position to the music record before the stop
static void stop_playback(bool save) {
    if (save) MusicSession_savePosition();
    Player_stop();
    Lyrics_clear();
    Playlist_free(&queue);
    source_dir_id = 0;
    source_playlist_id = 0;
    if (Background_getActive() == BG_MUSIC) Background_setActive(BG_NONE);
    ModuleCommon_setAutosleepDisabled(false);
}

// The queue ends on its own: no track is left, or no track starts
static void end_queue(void) {
    Resume_clear();
    stop_playback(false);
}

// Starts the queue at `index` with the recovery of the current step
static bool start_queue(int index, PendingSeek* seek) {
    if (Playlist_startWithRecovery(&queue, index, current_step(), start_track, seek) < 0) {
        end_queue();
        return false;
    }
    Background_setActive(BG_MUSIC);
    ModuleCommon_setAutosleepDisabled(true);
    return true;
}

// Stops the radio or a podcast before the music starts
static void take_audio(void) {
    if (Background_getActive() != BG_MUSIC) Background_stopAll();
}

// Moves `next` into the queue. The old queue stays until a new queue is ready, thus the music
// that plays keeps its queue when the build of the new one fails.
static void replace_queue(PlaylistContext* next) {
    take_audio();
    Playlist_free(&queue);
    queue = *next;
    next->tracks = NULL;
    next->track_count = 0;
}

static bool start_folder(int dir_id, int start_file_id, PendingSeek* seek) {
    PlaylistContext next = {0};
    if (Playlist_buildFromDirectory(&next, dir_id, start_file_id) <= 0 &&
        (start_file_id <= 0 || Playlist_buildFromFile(&next, start_file_id) <= 0)) {
        Playlist_free(&next);
        return false;
    }
    replace_queue(&next);
    source_dir_id = dir_id;
    source_playlist_id = 0;
    return start_queue(0, seek);
}

bool MusicSession_startFolder(int dir_id, int start_file_id) {
    return start_folder(dir_id, start_file_id, NULL);
}

static bool start_tracks(const PlaylistTrack* tracks, int track_count, int start_index,
                         int playlist_id, PendingSeek* seek) {
    PlaylistContext next = {0};
    Playlist_init(&next);
    if (!next.tracks) return false;
    int count = track_count < PLAYLIST_MAX_TRACKS ? track_count : PLAYLIST_MAX_TRACKS;
    memcpy(next.tracks, tracks, (size_t)count * sizeof(*tracks));
    next.track_count = count;
    replace_queue(&next);
    source_dir_id = 0;
    source_playlist_id = playlist_id;
    return start_queue(start_index >= 0 && start_index < count ? start_index : 0, seek);
}

bool MusicSession_startPlaylist(const PlaylistTrack* tracks, int track_count, int start_index,
                                int playlist_id) {
    if (!tracks || track_count <= 0) return false;
    return start_tracks(tracks, track_count, start_index, playlist_id, NULL);
}

bool MusicSession_startResume(const ResumeState* resume) {
    if (!resume) return false;
    // The start of a track writes the music record, thus the session reads a copy
    const ResumeState saved = *resume;
    PendingSeek seek = {
        .file_id     = saved.file_id,
        .position_ms = saved.position_ms,
    };

    if (saved.type == RESUME_TYPE_FILES) {
        return start_folder(saved.dir_id, saved.file_id, &seek);
    }
    if (saved.type != RESUME_TYPE_PLAYLIST) return false;

    static PlaylistTrack tracks[PLAYLIST_MAX_TRACKS];
    int count = M3U_loadTracks(saved.playlist_id, tracks, PLAYLIST_MAX_TRACKS);
    // A playlist that is gone ends Resume
    if (count < 0) {
        Resume_clear();
        return false;
    }
    if (count == 0) return false;
    int start_index = 0;
    for (int i = 0; i < count; i++) {
        if (tracks[i].file_id == saved.file_id) {
            start_index = i;
            break;
        }
    }
    return start_tracks(tracks, count, start_index, saved.playlist_id, &seek);
}

void MusicSession_tick(void) {
    if (queue.track_count <= 0) return;
    Player_update();

    PlayerState state = Player_getState();
    if (state == PLAYER_STATE_STOPPED) {
        if (Playlist_startWithRecovery(&queue, -1, current_step(), start_track, NULL) < 0) {
            end_queue();
        }
        return;
    }

    uint32_t now = SDL_GetTicks();
    if (state == PLAYER_STATE_PLAYING && now - last_position_save > POSITION_SAVE_MS) {
        Resume_updatePosition(Player_getPosition());
        last_position_save = now;
    }
}

// Pass true in `forward` to play the next track, and false to play the previous one
static void skip_track(bool forward) {
    if (queue.track_count <= 0) return;
    int count = queue.track_count;
    int index = (queue.current_index + (forward ? 1 : count - 1)) % count;
    MusicSession_savePosition();
    Player_stop();
    if (Playlist_startWithRecovery(&queue, index, current_step(), start_track, NULL) < 0) {
        end_queue();
    }
}

void MusicSession_next(void) {
    skip_track(true);
}

void MusicSession_prev(void) {
    skip_track(false);
}

void MusicSession_stop(void) {
    stop_playback(true);
}

void MusicSession_savePosition(void) {
    PlayerState state = Player_getState();
    if (queue.track_count > 0 && (state == PLAYER_STATE_PLAYING || state == PLAYER_STATE_PAUSED)) {
        Resume_updatePosition(Player_getPosition());
    }
}

void MusicSession_quit(void) {
    Playlist_free(&queue);
}

bool MusicSession_isActive(void) {
    return queue.track_count > 0;
}

unsigned MusicSession_getTrackSerial(void) {
    return track_serial;
}

int MusicSession_getTrackNumber(void) {
    return queue.track_count > 0 ? queue.current_index + 1 : 0;
}

int MusicSession_getTrackCount(void) {
    return queue.track_count;
}

bool MusicSession_isShuffle(void) {
    return shuffle_enabled;
}

bool MusicSession_isRepeat(void) {
    return repeat_enabled;
}

void MusicSession_toggleShuffle(void) {
    shuffle_enabled = !shuffle_enabled;
}

void MusicSession_toggleRepeat(void) {
    repeat_enabled = !repeat_enabled;
}
