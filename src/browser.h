#ifndef __BROWSER_H__
#define __BROWSER_H__

#include <stddef.h>
#include <stdbool.h>
#include "defines.h"
#include "player.h"

// File entry structure
typedef struct {
    int db_id;         // id of the dirs or files row, by is_dir; -1 for Play All
    char name[256];
    bool is_dir;
    bool is_play_all;  // Special "Play All" entry for folders with only subfolders
    AudioFormat format;
} FileEntry;

// Browser context structure
typedef struct {
    int data_version;
    char current_path[512];   // relative to the Music root, empty for the root
    int dir_id;
    bool is_root;         // the directory is the Music root, thus the list has no ".." entry
    bool root_no_music;   // no directory or a subdirectory holds music; set for the root only
    int audio_count;      // audio file entries in the list
    FileEntry* entries;
    int entry_count;
    int selected;
    int scroll_offset;
    int items_per_page;
} BrowserContext;

// Free browser entries
void Browser_freeEntries(BrowserContext* ctx);

// Load directory contents
void Browser_loadDirectory(BrowserContext* ctx, int dir_id);

// Returns true when the database changed after the list was built. Does not change ctx.
bool Browser_hasUpdate(const BrowserContext* ctx);

// Builds the list of the current directory again, and keeps the cursor on the same row.
// Returns false when the build failed; ctx then keeps the previous list.
bool Browser_refresh(BrowserContext* ctx);

#endif
