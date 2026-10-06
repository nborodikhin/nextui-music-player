#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "test.h"
#include "browser.h"
#include "db.h"
#include "file_utils.h"
#include "player.h"
#include "filedb.h"

#ifndef TEST_TEMP_DIR
#define TEST_TEMP_DIR "."
#endif

#ifndef FILEDB_MUSIC_PATH
#define FILEDB_MUSIC_PATH TEST_TEMP_DIR "/filedb-music"
#endif

#ifndef FILEDB_DATABASE_PATH
#define FILEDB_DATABASE_PATH TEST_TEMP_DIR "/filedb.db"
#endif

void LOG_note(int level, const char* format, ...) {
    (void)level;
    (void)format;
}

AudioFormat Player_detectFormat(const char* filepath) {
    const char* extension = strrchr(filepath, '.');
    if (!extension) return AUDIO_FORMAT_UNKNOWN;
    extension++;
    if (strcasecmp(extension, "mp3") == 0) return AUDIO_FORMAT_MP3;
    if (strcasecmp(extension, "wav") == 0) return AUDIO_FORMAT_WAV;
    if (strcasecmp(extension, "ogg") == 0) return AUDIO_FORMAT_OGG;
    if (strcasecmp(extension, "opus") == 0) return AUDIO_FORMAT_OPUS;
    if (strcasecmp(extension, "flac") == 0) return AUDIO_FORMAT_FLAC;
    if (strcasecmp(extension, "m4a") == 0) return AUDIO_FORMAT_M4A;
    if (strcasecmp(extension, "aac") == 0) return AUDIO_FORMAT_AAC;
    return AUDIO_FORMAT_UNKNOWN;
}

// Removes the playlists directory of the user data, thus no test sees the playlists of another.
static void remove_playlists(void) {
    char path[512];
    int length = userdata_snpath("playlists", path, sizeof(path));
    if (length >= 0 && (size_t)length < sizeof(path)) rm_rf(path);
}

static void reset_test_tree(void) {
    FileDb_quit();
    Db_quit();
    rm_rf(FILEDB_MUSIC_PATH);
    rm_rf(FILEDB_DATABASE_PATH);
    remove_playlists();
    mkdir_p(FILEDB_MUSIC_PATH);
}

static bool start_test(void) {
    reset_test_tree();
    return Db_initInternal(FILEDB_DATABASE_PATH);
}

static void stop_test(void) {
    FileDb_quit();
    Db_quit();
    rm_rf(FILEDB_MUSIC_PATH);
    rm_rf(FILEDB_DATABASE_PATH);
    remove_playlists();
}

static bool write_audio_file(const char* directory, const char* name) {
    char path[1024];
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path)) {
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    fputc(0, file);
    fclose(file);
    return true;
}

static bool wait_for_request(int id) {
    for (int i = 0; i < 5000; i++) {
        if (FileDb_isRequestDone(id)) return true;
        usleep(1000);
    }
    return false;
}

static bool wait_for_directory_scan(int dir_id, bool recursive) {
    int request = FileDb_scanDir(dir_id, recursive);
    return request > 0 && wait_for_request(request);
}

TEST(invalid_directory_scan_is_a_noop) {
    CHECK(FileDb_scanDir(-1, false) == 0);
    CHECK(!FileDb_waitBlocking(0));
}

TEST(wait_returns_false_while_worker_is_not_running) {
    CHECK(start_test());
    int request = FileDb_scanDir(Db_getRootDirId(), false);
    CHECK(request > 0);
    CHECK(!FileDb_waitBlocking(request));
    FileDb_start();
    CHECK(FileDb_waitBlocking(request));
    stop_test();
}

TEST(request_before_start_is_queued) {
    CHECK(start_test());
    int request = FileDb_scanDir(Db_getRootDirId(), false);
    CHECK(request > 0);
    FileDb_start();
    CHECK(FileDb_waitBlocking(request));
    stop_test();
}

TEST(three_requests_complete_in_order) {
    CHECK(start_test());
    FileDb_start();
    int first = FileDb_scanDir(Db_getRootDirId(), false);
    int second = FileDb_scanDir(Db_getRootDirId(), false);
    int third = FileDb_scanDir(Db_getRootDirId(), false);
    CHECK(first > 0 && second > first && third > second);
    CHECK(wait_for_request(third));
    CHECK(FileDb_isRequestDone(first));
    CHECK(FileDb_isRequestDone(second));
    stop_test();
}

TEST(listing_indexes_all_visible_files_and_hides_dotfiles) {
    CHECK(start_test());
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Albums"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "song.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "cover.jpg"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "notes.txt"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, ".hidden.mp3"));

    FileDb_start();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));

    DbDirResult* albums = Db_getDirByPath("Albums");
    DbFileResult* song = Db_getFileByPath("song.mp3");
    DbFileResult* cover = Db_getFileByPath("cover.jpg");
    DbFileResult* notes = Db_getFileByPath("notes.txt");
    DbFileResult* hidden = Db_getFileByPath(".hidden.mp3");
    DbReadDirResult* root = Db_readDir(Db_getRootDirId());
    DbFilesResult* playable = Db_getFiles(Db_getRootDirId(), DB_FILE_TYPE_MUSIC, false, 10, 0);
    BrowserContext browser = {0};
    Browser_loadDirectory(&browser, Db_getRootDirId());
    CHECK(albums);
    CHECK(song);
    CHECK(cover && cover->file.type == DB_FILE_TYPE_OTHER);
    CHECK(notes && notes->file.type == DB_FILE_TYPE_OTHER);
    CHECK(!hidden);
    CHECK(root && root->file_count == 3 && root->count == 4);
    CHECK(playable && playable->count == 1);
    CHECK(playable && playable->items &&
          strcmp(playable->items[0].filename, "song.mp3") == 0);
    CHECK(browser.entry_count == 3);
    CHECK(browser.entries && browser.entries[0].is_dir &&
          strcmp(browser.entries[0].name, "Albums") == 0);
    CHECK(browser.entries && !browser.entries[1].is_dir &&
          strcmp(browser.entries[1].name, "song.mp3") == 0);
    CHECK(browser.entries && browser.entries[2].is_play_all);
    CHECK(browser.audio_count == 1);
    Browser_freeEntries(&browser);
    Db_freeResult(albums);
    Db_freeResult(song);
    Db_freeResult(cover);
    Db_freeResult(notes);
    Db_freeResult(hidden);
    Db_freeResult(root);
    Db_freeResult(playable);
    stop_test();
}

TEST(unchanged_listing_does_not_move_data_version) {
    CHECK(start_test());
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "001 - Song .mp3"));
    FileDb_start();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    DbFileResult* file = Db_getFileByPath("001 - Song .mp3");
    CHECK(file && file->file.type == DB_FILE_TYPE_MUSIC);
    Db_freeResult(file);
    int version = Db_dataVersion();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));
    CHECK_EQ_INT(Db_dataVersion(), version);
    stop_test();
}

TEST(directory_removal_cascades_to_its_subtree) {
    CHECK(start_test());
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Albums/Blue"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/Albums/Blue", "song.mp3"));
    FileDb_start();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), true));
    DbDirResult* blue = Db_getDirByPath("Albums/Blue");
    CHECK(blue);
    Db_freeResult(blue);

    rm_rf(FILEDB_MUSIC_PATH "/Albums");
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));
    DbDirResult* albums = Db_getDirByPath("Albums");
    DbFileResult* song = Db_getFileByPath("Albums/Blue/song.mp3");
    CHECK(!albums);
    CHECK(!song);
    Db_freeResult(albums);
    Db_freeResult(song);
    stop_test();
}

TEST(kind_change_deletes_old_id_and_inserts_new_row) {
    CHECK(start_test());
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "Live.mp3"));
    FileDb_start();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));
    DbFileResult* old_file = Db_getFileByPath("Live.mp3");
    CHECK(old_file);
    Db_freeResult(old_file);

    remove(FILEDB_MUSIC_PATH "/Live.mp3");
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Live.mp3"));
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));
    DbFileResult* file = Db_getFileByPath("Live.mp3");
    DbDirResult* directory = Db_getDirByPath("Live.mp3");
    CHECK(!file);
    CHECK(directory);
    CHECK(directory && directory->dir.id > 0);
    Db_freeResult(file);
    Db_freeResult(directory);
    stop_test();
}

TEST(symlink_is_not_listed) {
    CHECK(start_test());
    char outside[512];
    snprintf(outside, sizeof(outside), "%s/filedb-outside", TEST_TEMP_DIR);
    rm_rf(outside);
    CHECK(mkdir_p(outside));
    CHECK(write_audio_file(outside, "outside.mp3"));
    CHECK(symlink(outside, FILEDB_MUSIC_PATH "/Link") == 0);

    FileDb_start();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));
    DbDirResult* link = Db_getDirByPath("Link");
    DbFileResult* outside_file = Db_getFileByPath("Link/outside.mp3");
    CHECK(!link);
    CHECK(!outside_file);
    Db_freeResult(link);
    Db_freeResult(outside_file);
    rm_rf(outside);
    stop_test();
}

TEST(stale_directory_rows_do_not_follow_symlink_ancestors) {
    CHECK(start_test());
    char outside[512];
    snprintf(outside, sizeof(outside), "%s/filedb-symlink-outside",
             TEST_TEMP_DIR);
    char outside_child[600];
    snprintf(outside_child, sizeof(outside_child), "%s/Child", outside);
    rm_rf(outside);
    CHECK(mkdir_p(outside_child));
    CHECK(write_audio_file(outside_child, "outside.mp3"));
    CHECK(symlink(outside, FILEDB_MUSIC_PATH "/Link") == 0);

    int link_id = 0;
    int child_id = 0;
    CHECK((link_id = Db_addDir(Db_getRootDirId(), "Link", "Link")) > 0);
    CHECK((child_id = Db_addDir(link_id, "Link/Child", "Child")) > 0);
    FileDb_start();
    int request = FileDb_scanDir(child_id, false);
    CHECK(request > 0 && wait_for_request(request));

    DbFileResult* outside_file = Db_getFileByPath("Link/Child/outside.mp3");
    CHECK(!outside_file);
    Db_freeResult(outside_file);
    rm_rf(outside);
    stop_test();
}

TEST(failed_directory_listing_keeps_its_row) {
    CHECK(start_test());
    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    int id = 0;
    CHECK((id = Db_addDir(Db_getRootDirId(), "/Bad", "Bad")) > 0);
    CHECK(wait_for_directory_scan(id, false));
    DbDirResult* bad = Db_getDir(id);
    CHECK(bad);
    Db_freeResult(bad);
    stop_test();
}

// Adds a file row, and sets its type as a file scan does. Returns its id, or 0 on error.
static int add_file_row(int parent_id, const char* filename, DbFileType type) {
    int id = Db_addFile(parent_id, filename);
    bool typed = type == DB_FILE_TYPE_UNKNOWN || Db_updateFileType(id, type);
    return id > 0 && typed ? id : 0;
}

TEST(directory_that_is_gone_loses_its_rows) {
    CHECK(start_test());
    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    int gone_id = 0;
    int child_id = 0;
    CHECK((gone_id = Db_addDir(Db_getRootDirId(), "Gone", "Gone")) > 0);
    CHECK((child_id = Db_addDir(gone_id, "Gone/Child", "Child")) > 0);
    CHECK(add_file_row(gone_id, "one.mp3", DB_FILE_TYPE_MUSIC) > 0);
    CHECK(add_file_row(child_id, "two.mp3", DB_FILE_TYPE_MUSIC) > 0);
    CHECK(wait_for_directory_scan(gone_id, false));

    DbDirResult* gone = Db_getDir(gone_id);
    DbDirResult* child = Db_getDir(child_id);
    DbFileResult* one = Db_getFileByPath("Gone/one.mp3");
    DbFileResult* two = Db_getFileByPath("Gone/Child/two.mp3");
    CHECK(!gone);
    CHECK(!child);
    CHECK(!one);
    CHECK(!two);
    Db_freeResult(gone);
    Db_freeResult(child);
    Db_freeResult(one);
    Db_freeResult(two);
    stop_test();
}

TEST(start_lists_playlists_and_counts_tracks) {
    CHECK(start_test());
    CHECK(userdata_mkdir("playlists"));
    char path[512];
    CHECK(userdata_snpath("playlists/mix.m3u", path, sizeof(path)) >= 0);
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file) {
        fputs("#EXTM3U\n#EXTINF:0,One\none.mp3\n#EXTINF:0,Two\ntwo.mp3\n", file);
        fclose(file);
    }

    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    DbPlaylistsResult* playlists = Db_readPlaylists();
    CHECK(playlists && playlists->count == 1);
    CHECK(playlists && playlists->count == 1 &&
          strcmp(playlists->items[0].path, "mix.m3u") == 0);
    CHECK(playlists && playlists->count == 1 && playlists->items[0].num_entries == 2);
    Db_freeResult(playlists);
    stop_test();
}

TEST(walk_lists_directories_breadth_first) {
    CHECK(start_test());
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/One/Deep"));
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Two/Deep"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "root.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/One", "one.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/Two", "two.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/One/Deep", "one-deep.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/Two/Deep", "two-deep.mp3"));

    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    CHECK(FileDb_isIdle());
    DbFileResult* root = Db_getFileByPath("root.mp3");
    DbFileResult* one = Db_getFileByPath("One/one.mp3");
    DbFileResult* two = Db_getFileByPath("Two/two.mp3");
    DbFileResult* one_deep = Db_getFileByPath("One/Deep/one-deep.mp3");
    DbFileResult* two_deep = Db_getFileByPath("Two/Deep/two-deep.mp3");
    CHECK(root);
    CHECK(one);
    CHECK(two);
    CHECK(one_deep);
    CHECK(two_deep);
    Db_freeResult(root);
    Db_freeResult(one);
    Db_freeResult(two);
    Db_freeResult(one_deep);
    Db_freeResult(two_deep);
    stop_test();
}

TEST(walk_starts_at_last_played_directory) {
    CHECK(start_test());
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Albums/Blue"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "root.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/Albums/Blue", "blue.mp3"));

    int albums_id = 0;
    int blue_id = 0;
    CHECK((albums_id = Db_addDir(Db_getRootDirId(), "Albums", "Albums")) > 0);
    CHECK((blue_id = Db_addDir(albums_id, "Albums/Blue", "Blue")) > 0);
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, blue_id, 0, NULL, 0));

    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    CHECK(FileDb_isIdle());
    DbFileResult* blue = Db_getFileByPath("Albums/Blue/blue.mp3");
    DbFileResult* root = Db_getFileByPath("root.mp3");
    CHECK(blue);
    CHECK(root);
    Db_freeResult(blue);
    Db_freeResult(root);
    stop_test();
}

static int file_id_at(const char* path) {
    DbFileResult* result = Db_getFileByPath(path);
    int id = result ? result->file.id : 0;
    Db_freeResult(result);
    return id;
}

TEST(walk_keeps_breadth_first_order_after_last_played_directory) {
    CHECK(start_test());
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Albums/Blue/Deep"));
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Zeta"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "root.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/Albums/Blue", "blue.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/Albums/Blue/Deep", "deep.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/Zeta", "zeta.mp3"));

    int albums_id = 0;
    int blue_id = 0;
    CHECK((albums_id = Db_addDir(Db_getRootDirId(), "Albums", "Albums")) > 0);
    CHECK((blue_id = Db_addDir(albums_id, "Albums/Blue", "Blue")) > 0);
    CHECK(Db_saveLastPlayed(DB_LAST_PLAYED_FOLDER, blue_id, 0, NULL, 0));

    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    CHECK(FileDb_isIdle());
    int blue = file_id_at("Albums/Blue/blue.mp3");
    int root = file_id_at("root.mp3");
    int zeta = file_id_at("Zeta/zeta.mp3");
    int deep = file_id_at("Albums/Blue/Deep/deep.mp3");
    CHECK(blue > 0 && root > blue);
    CHECK(zeta > root);
    CHECK(deep > zeta);
    stop_test();
}

TEST(file_scan_sets_type_of_unknown_file) {
    CHECK(start_test());
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "song.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "notes.txt"));
    int song_id = 0;
    int notes_id = 0;
    CHECK((song_id = add_file_row(Db_getRootDirId(), "song.mp3", DB_FILE_TYPE_UNKNOWN)) > 0);
    CHECK((notes_id = add_file_row(Db_getRootDirId(), "notes.txt", DB_FILE_TYPE_UNKNOWN)) > 0);
    FileDb_start();

    int request = FileDb_scanFile(song_id);
    CHECK(request > 0 && wait_for_request(request));
    request = FileDb_scanFile(notes_id);
    CHECK(request > 0 && wait_for_request(request));

    DbFileResult* song = Db_getFile(song_id);
    DbFileResult* notes = Db_getFile(notes_id);
    CHECK(song && song->file.type == DB_FILE_TYPE_MUSIC);
    CHECK(notes && notes->file.type == DB_FILE_TYPE_OTHER);
    Db_freeResult(song);
    Db_freeResult(notes);
    stop_test();
}

TEST(file_check_removes_gone_rows_and_keeps_present_rows) {
    CHECK(start_test());
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "present.mp3"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "gone.mp3"));
    FileDb_start();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));

    DbFileResult* present = Db_getFileByPath("present.mp3");
    DbFileResult* gone = Db_getFileByPath("gone.mp3");
    CHECK(present);
    CHECK(gone);
    int present_id = present ? present->file.id : 0;
    int gone_id = gone ? gone->file.id : 0;
    Db_freeResult(present);
    Db_freeResult(gone);

    remove(FILEDB_MUSIC_PATH "/gone.mp3");
    int request = FileDb_scanFile(gone_id);
    CHECK(request > 0 && wait_for_request(request));
    request = FileDb_scanFile(present_id);
    CHECK(request > 0 && wait_for_request(request));

    gone = Db_getFile(gone_id);
    present = Db_getFile(present_id);
    CHECK(!gone);
    CHECK(present);
    Db_freeResult(gone);
    Db_freeResult(present);
    stop_test();
}

TEST(file_check_removes_row_when_directory_takes_its_path) {
    CHECK(start_test());
    CHECK(write_audio_file(FILEDB_MUSIC_PATH, "Live.mp3"));
    FileDb_start();
    CHECK(wait_for_directory_scan(Db_getRootDirId(), false));
    DbFileResult* file = Db_getFileByPath("Live.mp3");
    CHECK(file);
    int file_id = file ? file->file.id : 0;
    Db_freeResult(file);

    remove(FILEDB_MUSIC_PATH "/Live.mp3");
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/Live.mp3"));
    int request = FileDb_scanFile(file_id);
    CHECK(request > 0 && wait_for_request(request));
    file = Db_getFile(file_id);
    CHECK(!file);
    Db_freeResult(file);
    stop_test();
}

TEST(playlist_check_removes_gone_playlist) {
    CHECK(start_test());
    CHECK(userdata_mkdir("playlists"));
    int playlist_id = 0;
    CHECK((playlist_id = Db_getOrCreatePlaylist("gone.m3u")) > 0);
    FileDb_start();
    int request = FileDb_scanPlaylist(playlist_id);
    CHECK(request > 0 && wait_for_request(request));
    DbPlaylistResult* playlist = Db_getPlaylist(playlist_id);
    CHECK(!playlist);
    Db_freeResult(playlist);
    stop_test();
}

TEST(playlists_request_adds_and_removes_rows) {
    CHECK(start_test());
    CHECK(userdata_mkdir("playlists"));
    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    CHECK(FileDb_isIdle());

    // The scan at the start is done, thus only the request can see this file.
    char path[512];
    CHECK(userdata_snpath("playlists/later.m3u", path, sizeof(path)) >= 0);
    FILE* m3u = fopen(path, "w");
    CHECK(m3u != NULL);
    if (m3u) {
        fputs("#EXTM3U\none.mp3\ntwo.mp3\n", m3u);
        fclose(m3u);
    }
    int request = FileDb_scanPlaylists();
    CHECK(request > 0 && wait_for_request(request));
    DbPlaylistsResult* added = Db_readPlaylists();
    CHECK(added && added->count == 1 && strcmp(added->items[0].path, "later.m3u") == 0);
    CHECK(added && added->count == 1 && added->items[0].num_entries == 2);
    Db_freeResult(added);

    CHECK(unlink(path) == 0);
    request = FileDb_scanPlaylists();
    CHECK(request > 0 && wait_for_request(request));
    DbPlaylistsResult* removed = Db_readPlaylists();
    CHECK(removed && removed->count == 0);
    Db_freeResult(removed);
    stop_test();
}

TEST(playlist_check_lists_the_directories_of_its_entries) {
    CHECK(start_test());
    FileDb_start();
    for (int i = 0; i < 5000 && !FileDb_isIdle(); i++) usleep(1000);
    CHECK(FileDb_isIdle());

    // The walk at the start is done, thus only the playlist check can index these files.
    CHECK(mkdir_p(FILEDB_MUSIC_PATH "/New/Deep"));
    CHECK(write_audio_file(FILEDB_MUSIC_PATH "/New/Deep", "song.mp3"));
    CHECK(userdata_mkdir("playlists"));
    char path[512];
    CHECK(userdata_snpath("playlists/new.m3u", path, sizeof(path)) >= 0);
    FILE* m3u = fopen(path, "w");
    CHECK(m3u != NULL);
    if (m3u) {
        fprintf(m3u, "#EXTM3U\n%s/New/Deep/song.mp3\n", FILEDB_MUSIC_PATH);
        fclose(m3u);
    }
    int playlist_id = 0;
    CHECK((playlist_id = Db_getOrCreatePlaylist("new.m3u")) > 0);
    CHECK(!Db_getFileByPath("New/Deep/song.mp3"));

    int request = FileDb_scanPlaylist(playlist_id);
    CHECK(request > 0 && wait_for_request(request));
    DbFileResult* song = Db_getFileByPath("New/Deep/song.mp3");
    CHECK(song && song->file.type == DB_FILE_TYPE_MUSIC);
    Db_freeResult(song);
    stop_test();
}

// Adds, under the Music root, the directory "A" and the files "a.mp3" and "b.mp3". The
// directory and "b.mp3" have the same id, 50. The browser list is then: A, a.mp3, b.mp3,
// Play All.
static bool add_rows_with_a_shared_id(void) {
    char sql[512];
    int root_id = Db_getRootDirId();
    snprintf(sql, sizeof(sql),
             "INSERT INTO dirs (id, parent_id, path, filename) VALUES (50, %d, 'A', 'A');"
             "INSERT INTO files (id, parent_id, filename, type) VALUES "
             "(50, %d, 'b.mp3', 'music'), (51, %d, 'a.mp3', 'music')",
             root_id, root_id, root_id);
    return Db_execute(sql);
}

// Returns the index of the entry with a name, or -1.
static int entry_index(const BrowserContext* browser, const char* name) {
    for (int i = 0; i < browser->entry_count; i++) {
        if (strcmp(browser->entries[i].name, name) == 0) return i;
    }
    return -1;
}

TEST(refresh_keeps_the_selected_file_when_a_directory_has_its_id) {
    CHECK(start_test());
    CHECK(add_rows_with_a_shared_id());
    BrowserContext browser = {0};
    Browser_loadDirectory(&browser, Db_getRootDirId());
    CHECK_EQ_INT(entry_index(&browser, "b.mp3"), 2);
    browser.selected = 2;

    // A new first file moves "b.mp3" one row down.
    CHECK(add_file_row(Db_getRootDirId(), "0.mp3", DB_FILE_TYPE_MUSIC) > 0);
    CHECK(Browser_hasUpdate(&browser));
    CHECK(Browser_refresh(&browser));
    CHECK_EQ_INT(browser.selected, 3);
    CHECK(browser.entries && !browser.entries[browser.selected].is_dir &&
          strcmp(browser.entries[browser.selected].name, "b.mp3") == 0);
    CHECK(!Browser_hasUpdate(&browser));
    Browser_freeEntries(&browser);
    stop_test();
}

TEST(refresh_keeps_the_selected_play_all_entry) {
    CHECK(start_test());
    CHECK(add_rows_with_a_shared_id());
    BrowserContext browser = {0};
    Browser_loadDirectory(&browser, Db_getRootDirId());
    browser.selected = browser.entry_count - 1;
    CHECK(browser.entries && browser.entries[browser.selected].is_play_all);

    CHECK(add_file_row(Db_getRootDirId(), "c.mp3", DB_FILE_TYPE_MUSIC) > 0);
    CHECK(Browser_refresh(&browser));
    CHECK(browser.entries && browser.entries[browser.selected].is_play_all);
    Browser_freeEntries(&browser);
    stop_test();
}

TEST(refresh_keeps_the_row_index_when_the_selected_file_is_gone) {
    CHECK(start_test());
    CHECK(add_rows_with_a_shared_id());
    BrowserContext browser = {0};
    Browser_loadDirectory(&browser, Db_getRootDirId());
    browser.selected = entry_index(&browser, "a.mp3");
    CHECK_EQ_INT(browser.selected, 1);

    CHECK(Db_deleteFile(51));
    CHECK(Browser_refresh(&browser));
    CHECK_EQ_INT(browser.selected, 1);
    CHECK(browser.entries && strcmp(browser.entries[1].name, "b.mp3") == 0);
    Browser_freeEntries(&browser);
    stop_test();
}

int main(void) {
    RUN(invalid_directory_scan_is_a_noop);
    RUN(wait_returns_false_while_worker_is_not_running);
    RUN(request_before_start_is_queued);
    RUN(three_requests_complete_in_order);
    RUN(listing_indexes_all_visible_files_and_hides_dotfiles);
    RUN(unchanged_listing_does_not_move_data_version);
    RUN(directory_removal_cascades_to_its_subtree);
    RUN(kind_change_deletes_old_id_and_inserts_new_row);
    RUN(symlink_is_not_listed);
    RUN(stale_directory_rows_do_not_follow_symlink_ancestors);
    RUN(failed_directory_listing_keeps_its_row);
    RUN(directory_that_is_gone_loses_its_rows);
    RUN(start_lists_playlists_and_counts_tracks);
    RUN(walk_lists_directories_breadth_first);
    RUN(walk_starts_at_last_played_directory);
    RUN(walk_keeps_breadth_first_order_after_last_played_directory);
    RUN(file_scan_sets_type_of_unknown_file);
    RUN(file_check_removes_gone_rows_and_keeps_present_rows);
    RUN(file_check_removes_row_when_directory_takes_its_path);
    RUN(playlist_check_removes_gone_playlist);
    RUN(playlists_request_adds_and_removes_rows);
    RUN(playlist_check_lists_the_directories_of_its_entries);
    RUN(refresh_keeps_the_selected_file_when_a_directory_has_its_id);
    RUN(refresh_keeps_the_selected_play_all_entry);
    RUN(refresh_keeps_the_row_index_when_the_selected_file_is_gone);
    return test_summary();
}
