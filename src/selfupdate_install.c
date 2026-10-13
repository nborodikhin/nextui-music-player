#include "selfupdate.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_utils.h"

// Written on the device rather than shipped, keyed by their path relative to the
// install directory. The pak of an update does not carry them, so the removal
// must not treat them as leftovers: the binaries cost tens of megabytes to re-fetch,
// and the queue is the user's own pending work.
// state/yt-dlp_version.txt is deliberately absent: it is a cache the next launch
// rebuilds from the binary.
static const char* const preserved_paths[] = {
    "bin/yt-dlp",
    "bin/qjs",
    "bin/ffmpeg",
    "state/youtube_queue.txt",
    NULL
};

static bool is_preserved(const char* rel_path) {
    for (int i = 0; preserved_paths[i]; i++) {
        if (strcmp(preserved_paths[i], rel_path) == 0) return true;
    }
    return false;
}

static bool has_preserved_child(const char* rel_path) {
    size_t length = strlen(rel_path);
    for (int i = 0; preserved_paths[i]; i++) {
        if (strncmp(preserved_paths[i], rel_path, length) == 0 &&
            preserved_paths[i][length] == '/') return true;
    }
    return false;
}

// rel is the path of dst relative to the install directory ("" at the top level).
static void remove_obsolete_files(const char* src, const char* dst, const char* rel) {
    DIR* dir = opendir(dst);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char src_path[600], dst_path[600], rel_path[600];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        snprintf(rel_path, sizeof(rel_path), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);

        if (access(src_path, F_OK) != 0) {
            if (is_preserved(rel_path)) {
                continue;
            }
            struct stat status;
            if (lstat(dst_path, &status) == 0 && S_ISDIR(status.st_mode) &&
                has_preserved_child(rel_path)) {
                remove_obsolete_files(src_path, dst_path, rel_path);
            } else {
                rm_rf(dst_path);
            }
        }
        else if (entry->d_type == DT_DIR) {
            remove_obsolete_files(src_path, dst_path, rel_path);
        }
    }

    closedir(dir);
}

void SelfUpdate_removeObsoleteFiles(const char* pak_dir, const char* install_dir) {
    if (!pak_dir || !install_dir) return;
    remove_obsolete_files(pak_dir, install_dir, "");
}
