#include "browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"
#include "player.h"

void Browser_freeEntries(BrowserContext* ctx) {
    free(ctx->entries);
    ctx->entries = NULL;
    ctx->entry_count = 0;
}

// Copies a filename to out. Returns false when it does not fit.
static bool copy_filename(const char* filename, char* out, size_t out_size) {
    int length = snprintf(out, out_size, "%s", filename);
    return length >= 0 && (size_t)length < out_size;
}

// Returns true when two entries show the same row: the ids of directories and of files are
// separate, thus the kind of entry is part of the match.
static bool same_entry(const FileEntry* a, const FileEntry* b) {
    return a->db_id == b->db_id && a->is_dir == b->is_dir && a->is_play_all == b->is_play_all;
}

static bool build_directory(BrowserContext* ctx, int dir_id, bool preserve_selection) {
    DbReadDirResult* readDirResult = Db_readDir(dir_id);
    if (!readDirResult) {
        Db_freeResult(readDirResult);
        return false;
    }

    FileEntry old_selected_entry = { .db_id = 0 };
    int old_selected = -1;
    if (preserve_selection && ctx->selected >= 0 && ctx->selected < ctx->entry_count) {
        old_selected = ctx->selected;
        old_selected_entry = ctx->entries[old_selected];
    }

    // At most the parent, the directories, the files and Play All.
    int capacity = 1 + readDirResult->dir_count + readDirResult->file_count + 1;
    FileEntry* entries = calloc((size_t)capacity, sizeof(*entries));
    int count = 0;
    bool success = entries != NULL;

    DbDir directory = readDirResult->dir;

    if (success && directory.parent_id >= 0) {
        entries[count++] = (FileEntry){
            .db_id       = directory.parent_id,
            .name        = "..",
            .is_dir      = true,
            .is_play_all = false,
            .format      = AUDIO_FORMAT_UNKNOWN,
        };
    }

    for (int i = 0; success && i < readDirResult->dir_count; i++) {
        DbDir* dir = &readDirResult->dirs[i];
        FileEntry entry = {
            .db_id       = dir->id,
            .name        = {0},
            .is_dir      = true,
            .is_play_all = false,
            .format      = AUDIO_FORMAT_UNKNOWN,
        };
        success = copy_filename(dir->filename, entry.name, sizeof(entry.name));
        if (success) entries[count++] = entry;
    }

    for (int i = 0; success && i < readDirResult->file_count; i++) {
        DbFile* file = &readDirResult->files[i];
        if (file->type != DB_FILE_TYPE_MUSIC) continue;
        AudioFormat format = Player_detectFormat(file->filename);
        FileEntry entry = {
            .db_id       = file->id,
            .name        = {0},
            .is_dir      = false,
            .is_play_all = false,
            .format      = format,
        };
        success = copy_filename(file->filename, entry.name, sizeof(entry.name));
        if (success) entries[count++] = entry;
    }

    if (success && readDirResult->dir_count > 0) {
        entries[count++] = (FileEntry){
            .db_id       = -1,
            .name        = "Play All",
            .is_dir      = false,
            .is_play_all = true,
            .format      = AUDIO_FORMAT_UNKNOWN,
        };
    }

    if (success) {
        int selected = 0;
        if (old_selected >= 0) {
            bool found = false;
            for (int i = 0; i < count; i++) {
                if (same_entry(&entries[i], &old_selected_entry)) {
                    selected = i;
                    found = true;
                    break;
                }
            }
            if (!found && count > 0) {
                selected = old_selected < count ? old_selected : count - 1;
            }
        }
        Browser_freeEntries(ctx);
        ctx->data_version = readDirResult->base.data_version;
        ctx->entries = entries;
        ctx->entry_count = count;
        ctx->selected = selected;
        ctx->scroll_offset = 0;
        ctx->dir_id = dir_id;
        ctx->is_root = directory.parent_id < 0;
        ctx->audio_count = 0;
        for (int i = 0; i < count; i++) {
            if (!entries[i].is_dir && !entries[i].is_play_all) ctx->audio_count++;
        }
        ctx->root_no_music = ctx->is_root &&
                             Db_countFiles(dir_id, DB_FILE_TYPE_MUSIC, true) == 0;
        // The path is for display only, thus a long path is cut.
        snprintf(ctx->current_path, sizeof(ctx->current_path), "%s", directory.path);
    } else {
        free(entries);
    }

    Db_freeResult(readDirResult);
    return success;
}

void Browser_loadDirectory(BrowserContext* ctx, int dir_id) {
    build_directory(ctx, dir_id, false);
}

bool Browser_hasUpdate(const BrowserContext* ctx) {
    return ctx && Db_dataVersion() != ctx->data_version;
}

bool Browser_refresh(BrowserContext* ctx) {
    if (!ctx) return false;
    return build_directory(ctx, ctx->dir_id, true);
}
