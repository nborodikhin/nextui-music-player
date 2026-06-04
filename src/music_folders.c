#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "defines.h"
#include "music_folders.h"

#define FOLDERS_DIR  SHARED_USERDATA_PATH "/music-player"
#define FOLDERS_FILE SHARED_USERDATA_PATH "/music-player/folders.txt"
#define DEFAULT_FOLDER SDCARD_PATH "/Music"

static char folders[MUSIC_FOLDERS_MAX][512];
static int folder_count = 0;
static bool initialized = false;

const char* MusicFolders_defaultPath(void) {
    return DEFAULT_FOLDER;
}

// Strip trailing slashes (except a lone "/") so comparisons are consistent.
static void normalize_path(char* path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
}

// True if "a" and "b" are the same path, or one is a parent directory of the other.
// Used to reject overlapping folders that would list the same tracks twice.
static bool paths_overlap(const char* a, const char* b) {
    if (strcmp(a, b) == 0) return true;

    size_t la = strlen(a);
    size_t lb = strlen(b);
    // a is an ancestor of b: b starts with "a/"
    if (lb > la && strncmp(b, a, la) == 0 && b[la] == '/') return true;
    // b is an ancestor of a: a starts with "b/"
    if (la > lb && strncmp(a, b, lb) == 0 && a[lb] == '/') return true;
    return false;
}

static bool list_contains(const char* path) {
    for (int i = 0; i < folder_count; i++) {
        if (strcmp(folders[i], path) == 0) return true;
    }
    return false;
}

static void save(void) {
    mkdir(FOLDERS_DIR, 0755);
    FILE* f = fopen(FOLDERS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < folder_count; i++) {
        fprintf(f, "%s\n", folders[i]);
    }
    fclose(f);
}

void MusicFolders_init(void) {
    if (initialized) return;
    initialized = true;
    folder_count = 0;

    FILE* f = fopen(FOLDERS_FILE, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f) && folder_count < MUSIC_FOLDERS_MAX) {
            // Strip newline
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            char* cr = strchr(line, '\r');
            if (cr) *cr = '\0';
            if (line[0] == '\0') continue;
            normalize_path(line);
            if (list_contains(line)) continue;
            snprintf(folders[folder_count], sizeof(folders[folder_count]), "%s", line);
            folder_count++;
        }
        fclose(f);
    }

    // Ensure the pinned default folder is always present, at the front.
    if (!list_contains(DEFAULT_FOLDER)) {
        if (folder_count >= MUSIC_FOLDERS_MAX) folder_count = MUSIC_FOLDERS_MAX - 1;
        for (int i = folder_count; i > 0; i--) {
            snprintf(folders[i], sizeof(folders[i]), "%s", folders[i - 1]);
        }
        snprintf(folders[0], sizeof(folders[0]), "%s", DEFAULT_FOLDER);
        folder_count++;
        save();
    }
}

int MusicFolders_count(void) {
    return folder_count;
}

const char* MusicFolders_get(int i) {
    if (i < 0 || i >= folder_count) return NULL;
    return folders[i];
}

bool MusicFolders_existsAt(int i) {
    if (i < 0 || i >= folder_count) return false;
    struct stat st;
    return (stat(folders[i], &st) == 0 && S_ISDIR(st.st_mode));
}

bool MusicFolders_isPinned(int i) {
    if (i < 0 || i >= folder_count) return false;
    return strcmp(folders[i], DEFAULT_FOLDER) == 0;
}

bool MusicFolders_add(const char* path) {
    if (!path || path[0] == '\0') return false;
    if (folder_count >= MUSIC_FOLDERS_MAX) return false;

    char clean[512];
    snprintf(clean, sizeof(clean), "%s", path);
    normalize_path(clean);
    if (clean[0] == '\0') return false;

    for (int i = 0; i < folder_count; i++) {
        if (paths_overlap(folders[i], clean)) return false;
    }

    snprintf(folders[folder_count], sizeof(folders[folder_count]), "%s", clean);
    folder_count++;
    save();
    return true;
}

bool MusicFolders_removeAt(int i) {
    if (i < 0 || i >= folder_count) return false;
    if (MusicFolders_isPinned(i)) return false;
    if (folder_count <= 1) return false;  // never empty

    for (int j = i; j < folder_count - 1; j++) {
        snprintf(folders[j], sizeof(folders[j]), "%s", folders[j + 1]);
    }
    folder_count--;
    save();
    return true;
}
