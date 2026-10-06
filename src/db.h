#ifndef __DB_H__
#define __DB_H__

#include <stdbool.h>
#include <stdint.h>

typedef void (*DbResultDestructor)(void* data);

typedef struct DbResult {
    int data_version;
    DbResultDestructor free;
} DbResult;

/* Initializes the database at the standard app data path. */
bool Db_init(void);
/* Closes the database connections of the process. */
void Db_quit(void);
/* Returns whether the database is open. */
bool Db_isAvailable(void);

/* Starts a transaction on the calling thread. */
bool Db_begin(void);
/* Commits the transaction on the calling thread. */
bool Db_commit(void);
/* Rolls back the transaction on the calling thread. */
bool Db_rollback(void);
/* Executes SQL without returning rows. */
bool Db_execute(const char* sql);

/* Returns the current process data version. */
int Db_dataVersion(void);

/* Returns whether a result has the current data version. */
bool Db_resultIsCurrent(const DbResult* result);
/* Free a result - any result based on a DbResult */
void Db_freeResult(void* result);

typedef enum {
    DB_SETTING_INT,
    DB_SETTING_BOOL,
    DB_SETTING_STRING,
} DbSettingType;

typedef struct {
    char*         name;
    DbSettingType type;
    int           int_value;
    bool          bool_value;
    char*         string_value;
} DbSetting;

typedef struct {
    DbResult   base;
    DbSetting* items;
    int        count;
} DbSettingsResult;

DbSettingsResult* Db_readSettings(void);
bool               Db_saveIntSetting(const char* name, int value);
bool               Db_saveBoolSetting(const char* name, bool value);
bool               Db_saveStringSetting(const char* name, const char* value);

// Read file entry

typedef enum {
    DB_FILE_TYPE_UNKNOWN = 0,   // not yet checked
    DB_FILE_TYPE_MUSIC,
    DB_FILE_TYPE_OTHER,
    DB_FILE_TYPE_ALL,           // for queries only, matches any type
} DbFileType;

typedef struct {
    int          id;
    int          parent_id;
    char*        dir_path;   // the directory of the file, relative to the Music root
    char*        path;       // the file, relative to the Music root
    char*        filename;
    DbFileType   type;
} DbFile;

typedef struct {
    DbResult      base;
    DbFile        file;
} DbFileResult;

// NULL when file is not found in the db
DbFileResult*      Db_getFile(int id);
DbFileResult*      Db_getFileByPath(const char* path);
DbFileResult*      Db_getFileInDir(int dir_id, const char* filename);

// Adds a file with the type unknown; a file scan sets its type. Returns the id of the new row,
// or 0 on error.
int Db_addFile(int parent_id, const char* filename);
bool Db_deleteFile(int id);
bool Db_updateFileType(int id, DbFileType type);

// Read dir entry

typedef struct {
    int   id;
    int   parent_id;
    char* path;
    char* filename;
} DbDir;

typedef struct {
    DbResult      base;
    DbDir         dir;
} DbDirResult;

typedef struct {
    DbResult base;
    DbDir    dir;         // the directory itself
    DbDir*   dirs;        // subdirectories
    DbFile*  files;
    int      dir_count;
    int      file_count;
    int      count;       // total entries: dir_count + file_count
} DbReadDirResult;

// Returns the id of the Music root, which is the directory with no parent, or -1
// when the database has no root.
int                Db_getRootDirId(void);
DbDirResult*       Db_getDir(int id);
DbDirResult*       Db_getDirByPath(const char* path);
// Returns NULL when the directory does not exist or the read failed.
DbReadDirResult*   Db_readDir(int dir_id);
// Returns the id of the new row, or 0 on error.
int Db_addDir(int parent_id, const char* path, const char* filename);
bool Db_deleteDir(int id);

// Files of directories

typedef struct {
    DbResult base;
    DbFile*  items;
    int      count;
    bool     has_more;
} DbFilesResult;

// Returns the number of files of `type` in a directory, or 0 on error. Pass true in recursive to
// count the files in each directory below it as well. Pass DB_FILE_TYPE_ALL in type to count
// the files of each type.
int                Db_countFiles(int dir_id, DbFileType type, bool recursive);
// Returns the files of type in a directory, and in the directories below it when recursive is
// true. Pass DB_FILE_TYPE_ALL in type to return the files of each type. NULL when dir_id is not
// found in the db.
DbFilesResult*     Db_getFiles(int dir_id, DbFileType type, bool recursive, int max_count,
                               int token);

// Playlist

typedef struct {
    int   id;
    char* path;
    int   num_entries;
} DbPlaylist;

typedef struct {
    DbResult      base;
    DbPlaylist    playlist;
} DbPlaylistResult;

typedef struct {
    DbResult     base;
    DbPlaylist*  items;
    int          count;
} DbPlaylistsResult;

DbPlaylistResult*  Db_getPlaylist(int id);
DbPlaylistsResult* Db_readPlaylists(void);
// Returns the id of the playlist row with path, and adds the row when there is none. Returns 0
// on error.
int Db_getOrCreatePlaylist(const char* path);
bool Db_deletePlaylist(int id);
bool Db_updatePlaylist(int id, int num_entries);

// Last played records

typedef enum {
    DB_LAST_PLAYED_FOLDER   = 0,
    DB_LAST_PLAYED_PLAYLIST = 1,
} DbLastPlayedType;

typedef struct {
    DbLastPlayedType type;
    int              id1;
    int              id2;
    int              position;
    char             track_name[256];   // the title of the track, empty when not known
} DbLastPlayed;

typedef struct {
    DbResult      base;
    DbLastPlayed  last_played;
} DbLastPlayedResult;

// Returns the record that the main menu offers to resume, or NULL when there is none.
DbLastPlayedResult* Db_readLastPlayed(void);
// Pass NULL or an empty string in track_name when the track has no name.
bool Db_saveLastPlayed(DbLastPlayedType type, int id1, int id2, const char* track_name,
                       int position);
bool Db_saveLastPlayedPosition(int position);
bool Db_clearLastPlayed(void);

// Test support code
/* Initializes the database at an explicit path for internal tests. */
bool Db_initInternal(const char* path);

#endif
