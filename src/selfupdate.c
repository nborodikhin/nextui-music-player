#include "selfupdate.h"
#include "wget_fetch.h"
#include "file_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <zip.h>

#include "include/parson/parson.h"
#include "defines.h"
#include "api.h"

#define RELEASE_JSON_MAX 32768

// How the progress bar is shared out. Download dominates, but unpacking and
// installing move tens of megabytes on and off the SD card and are slow enough
// that a bar frozen at one number reads as a hang.
#define EXTRACT_BASE_PCT 45
#define EXTRACT_SPAN_PCT 20
#define APPLY_BASE_PCT   70
#define APPLY_SPAN_PCT   20

// Paths
static char pak_path[512] = "";
static char version_file[512] = "";
static char current_version[32] = "";

// Update status
static SelfUpdateStatus update_status = {0};
static pthread_t update_thread;
static volatile bool update_running = false;
static volatile bool update_cancel = false;

// Files the last extract_zip() wrote out, which is what the install then copies
static int extracted_files = 0;

// Forward declarations
static void* check_thread_func(void* arg);
static void* update_thread_func(void* arg);

// Fetch a URL into a freshly allocated, NUL-terminated buffer the caller owns and must free.
//
// @param max_size  Largest body to accept, terminator included
// @return          the body, or NULL if it could not be fetched
static char* fetch_to_memory(const char* url, size_t max_size) {
    char* buf = malloc(max_size);
    if (!buf) return NULL;

    if (wget_fetch_string(url, buf, (int)max_size) < 0) {
        free(buf);
        return NULL;
    }

    return buf;
}

// Report bytes landed so far against update_status.download_total, scaling the
// transfer into the first 40% of the update.
//
// @return  false once the user has cancelled, which stops the transfer
static bool report_download_progress(long written, int speed_bps, void* ctx) {
    (void)speed_bps;
    (void)ctx;

    update_status.download_bytes = written;

    if (update_status.download_total > 0) {
        int dl_pct = (int)((written * 100) / update_status.download_total);
        if (dl_pct > 100) dl_pct = 100;
        update_status.progress_percent = (dl_pct * 40) / 100;
    }

    snprintf(update_status.status_detail, sizeof(update_status.status_detail),
        "%.1f MB / %.1f MB", written / (1024.0 * 1024.0),
        update_status.download_total / (1024.0 * 1024.0));

    return !update_cancel;
}

// Compare semantic versions: returns positive if v1 > v2, negative if v1 < v2, 0 if equal
static int compare_versions(const char* v1, const char* v2) {
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;

    // Skip 'v' prefix if present
    if (v1[0] == 'v' || v1[0] == 'V') v1++;
    if (v2[0] == 'v' || v2[0] == 'V') v2++;

    sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

// Helper function to create directory path recursively
static int mkpath(const char* path, mode_t mode) {
    char tmp[512];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode);
}

// Written on the device rather than shipped, keyed by their path relative to the
// pak root. They are absent from the package on purpose, so orphan removal must
// not treat them as leftovers: the binaries cost tens of megabytes to re-fetch,
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

// Drives the install slice of the progress bar from the file count the extract
// just reported.
static void note_file_installed(const char* rel_path, void* ctx) {
    (void)rel_path;
    (void)ctx;

    int done = ++*(int*)ctx;
    if (extracted_files > 0) {
        int pct = (int)((long long)done * 100 / extracted_files);
        if (pct > 100) pct = 100;
        update_status.progress_percent = APPLY_BASE_PCT + (APPLY_SPAN_PCT * pct) / 100;
    }

    snprintf(update_status.status_detail, sizeof(update_status.status_detail),
        "%d / %d files", done, extracted_files);
}

// Delete anything in dst that the update no longer carries, except the paths
// written on the device rather than shipped.
// rel is the path of dst relative to the pak root ("" at the top level).
static void remove_orphans(const char* src, const char* dst, const char* rel) {
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
            rm_rf(dst_path);
        }
        else if (entry->d_type == DT_DIR) {
            remove_orphans(src_path, dst_path, rel_path);
        }
    }

    closedir(dir);
}

// Install the unpacked update over the pak: copy everything across, then drop
// whatever the new package no longer has.
static int sync_directories(const char* src, const char* dst) {
    int installed = 0;
    if (!cp_rf(src, dst, note_file_installed, &installed)) return -1;

    remove_orphans(src, dst, "");
    return 0;
}

// Reject an archive entry that would land outside the directory being extracted
// into: an absolute path, or one that walks up out of it. A package we build
// contains neither, so anything that does is not a package we should unpack.
static bool zip_entry_is_contained(const char* name) {
    if (name[0] == '/') return false;

    for (const char* part = name; *part; ) {
        const char* slash = strchr(part, '/');
        size_t len = slash ? (size_t)(slash - part) : strlen(part);

        if (len == 2 && part[0] == '.' && part[1] == '.') return false;
        if (!slash) break;
        part = slash + 1;
    }

    return true;
}

// Extract ZIP file using libzip, driving the extract slice of the progress bar
// from the entry count. Sets extracted_files.
//
// Any failure fails the whole extraction: a partial tree looks like a valid
// package to the install that follows, which would then copy it over the pak and
// delete everything the truncated archive did not mention.
//
// @return 0 on success, -1 if the archive could not be unpacked in full
static int extract_zip(const char* zip_path, const char* dest_dir) {
    int err = 0;
    zip_t* za = zip_open(zip_path, 0, &err);
    if (!za) {
        return -1;
    }

    extracted_files = 0;
    bool ok = true;

    zip_int64_t num_entries = zip_get_num_entries(za, 0);
    for (zip_int64_t i = 0; ok && i < num_entries; i++) {
        update_status.progress_percent = EXTRACT_BASE_PCT +
            (int)((long long)EXTRACT_SPAN_PCT * (i + 1) / num_entries);
        snprintf(update_status.status_detail, sizeof(update_status.status_detail),
            "%lld / %lld files", (long long)(i + 1), (long long)num_entries);

        const char* name = zip_get_name(za, i, 0);
        if (!name || !zip_entry_is_contained(name)) {
            LOG_error("[SelfUpdate] Refusing archive entry: %s\n", name ? name : "(unnamed)");
            ok = false;
            break;
        }

        char full_path[600];
        snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, name);

        // Check if it's a directory
        size_t name_len = strlen(name);
        if (name_len > 0 && name[name_len - 1] == '/') {
            mkpath(full_path, 0755);
            continue;
        }

        // Create parent directory if needed
        char* last_slash = strrchr(full_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkpath(full_path, 0755);
            *last_slash = '/';
        }

        // Extract file
        zip_file_t* zf = zip_fopen_index(za, i, 0);
        if (!zf) {
            LOG_error("[SelfUpdate] Could not read %s from the archive\n", name);
            ok = false;
            break;
        }

        FILE* out = fopen(full_path, "wb");
        if (!out) {
            LOG_error("[SelfUpdate] Could not write %s\n", full_path);
            zip_fclose(zf);
            ok = false;
            break;
        }

        char buf[8192];
        zip_int64_t bytes_read;
        while ((bytes_read = zip_fread(zf, buf, sizeof(buf))) > 0) {
            if (fwrite(buf, 1, (size_t)bytes_read, out) != (size_t)bytes_read) {
                LOG_error("[SelfUpdate] Short write extracting %s\n", name);
                ok = false;
                break;
            }
        }
        if (bytes_read < 0) {
            LOG_error("[SelfUpdate] Corrupt archive entry: %s\n", name);
            ok = false;
        }

        if (fclose(out) != 0) ok = false;
        zip_fclose(zf);

        if (!ok) break;

        // Preserve executable permission for .elf and .sh files
        if (strstr(name, ".elf") || strstr(name, ".sh")) {
            chmod(full_path, 0755);
        }

        extracted_files++;
    }

    zip_close(za);
    return ok ? 0 : -1;
}

int SelfUpdate_init(const char* path) {
    if (!path) return -1;

    strncpy(pak_path, path, sizeof(pak_path) - 1);

    // Set up paths
    snprintf(version_file, sizeof(version_file), "%s/state/app_version.txt", pak_path);

    // Read version from file (primary source)
    strncpy(current_version, APP_VERSION_FALLBACK, sizeof(current_version) - 1);
    FILE* f = fopen(version_file, "r");
    if (f) {
        char file_version[32] = "";
        if (fgets(file_version, sizeof(file_version), f)) {
            char* nl = strchr(file_version, '\n');
            if (nl) *nl = '\0';
            if (strlen(file_version) > 0) {
                strncpy(current_version, file_version, sizeof(current_version) - 1);
            }
        }
        fclose(f);
    }

    memset(&update_status, 0, sizeof(update_status));
    strncpy(update_status.current_version, current_version, sizeof(update_status.current_version));

    return 0;
}

void SelfUpdate_cleanup(void) {
    if (update_running) {
        update_cancel = true;
        pthread_join(update_thread, NULL);
    }
}

const char* SelfUpdate_getVersion(void) {
    return current_version;
}

int SelfUpdate_checkForUpdate(void) {
    if (update_running) return -1;

    update_cancel = false;
    update_running = true;

    memset(&update_status, 0, sizeof(update_status));
    update_status.state = SELFUPDATE_STATE_CHECKING;
    strncpy(update_status.current_version, current_version, sizeof(update_status.current_version));
    strcpy(update_status.status_message, "Checking for updates...");

    if (pthread_create(&update_thread, NULL, check_thread_func, NULL) != 0) {
        update_running = false;
        update_status.state = SELFUPDATE_STATE_ERROR;
        strcpy(update_status.error_message, "Failed to start update check");
        return -1;
    }

    return 0;
}

int SelfUpdate_startUpdate(void) {
    if (update_running) return -1;
    if (!update_status.update_available) return -1;

    update_cancel = false;
    update_running = true;

    update_status.state = SELFUPDATE_STATE_DOWNLOADING;
    update_status.progress_percent = 0;
    strcpy(update_status.status_message, "Starting download...");

    if (pthread_create(&update_thread, NULL, update_thread_func, NULL) != 0) {
        update_running = false;
        update_status.state = SELFUPDATE_STATE_ERROR;
        strcpy(update_status.error_message, "Failed to start update");
        return -1;
    }

    return 0;
}

void SelfUpdate_cancelUpdate(void) {
    if (update_running) {
        update_cancel = true;
    }
}

const SelfUpdateStatus* SelfUpdate_getStatus(void) {
    return &update_status;
}

SelfUpdateStatus SelfUpdate_getSnapshot(void) {
    return update_status;
}

UpdateUiState SelfUpdate_uiState(const SelfUpdateStatus* status) {
    if (!status) return UPDATE_UI_UNCHECKED;

    if (status->state == SELFUPDATE_STATE_CHECKING) return UPDATE_UI_CHECKING;
    if (status->state == SELFUPDATE_STATE_ERROR) return UPDATE_UI_FAILED;
    if (status->update_available) return UPDATE_UI_AVAILABLE;

    // latest_version is only set once a check has come back
    if (status->latest_version[0] != '\0') return UPDATE_UI_CURRENT;

    return UPDATE_UI_UNCHECKED;
}

void SelfUpdate_update(void) {
    // Check if thread has finished
    if (update_running) {
        // Thread is still running, nothing to do
    }
}

bool SelfUpdate_isPendingRestart(void) {
    return update_status.state == SELFUPDATE_STATE_COMPLETED;
}

void SelfUpdate_requestRestart(void) {
    FILE* f = fopen(SELFUPDATE_RESTART_FLAG, "w");
    if (!f) {
        LOG_error("Could not write %s, app will not restart itself\n", SELFUPDATE_RESTART_FLAG);
        return;
    }
    fclose(f);
}

SelfUpdateState SelfUpdate_getState(void) {
    return update_status.state;
}

// Check for update thread
static void* check_thread_func(void* arg) {
    (void)arg;

    // Check connectivity
    int conn = system("ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1");
    if (conn != 0) {
        conn = system("ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1");
    }

    if (conn != 0) {
        strcpy(update_status.error_message, "No internet connection");
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    if (update_cancel) {
        update_status.state = SELFUPDATE_STATE_IDLE;
        update_running = false;
        return NULL;
    }

    update_status.progress_percent = 20;

    // Fetch latest release info from GitHub API
    char api_url[256];
    snprintf(api_url, sizeof(api_url),
        "https://api.github.com/repos/%s/releases/latest", APP_GITHUB_REPO);

    char* release_json = fetch_to_memory(api_url, RELEASE_JSON_MAX);
    if (!release_json) {
        strcpy(update_status.error_message, "Failed to check GitHub");
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    if (update_cancel) {
        free(release_json);
        update_status.state = SELFUPDATE_STATE_IDLE;
        update_running = false;
        return NULL;
    }

    update_status.progress_percent = 50;

    JSON_Value* json_root = json_parse_string(release_json);
    free(release_json);

    JSON_Object* release = json_root ? json_value_get_object(json_root) : NULL;
    const char* latest_version = release ? json_object_get_string(release, "tag_name") : NULL;

    if (!latest_version || latest_version[0] == '\0') {
        json_value_free(json_root);
        strcpy(update_status.error_message, "Could not parse version");
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    strncpy(update_status.latest_version, latest_version, sizeof(update_status.latest_version) - 1);

    update_status.progress_percent = 70;

    // Compare versions using semantic versioning
    if (compare_versions(latest_version, current_version) <= 0) {
        json_value_free(json_root);
        update_status.update_available = false;
        strcpy(update_status.status_message, "Already up to date");
        update_status.state = SELFUPDATE_STATE_IDLE;
        update_running = false;
        return NULL;
    }

    // Locate the pak.zip among the release assets
    const char* download_url = NULL;
    JSON_Array* assets = json_object_get_array(release, "assets");
    for (size_t i = 0; assets && i < json_array_get_count(assets); i++) {
        JSON_Object* asset = json_array_get_object(assets, i);
        const char* name = asset ? json_object_get_string(asset, "name") : NULL;
        if (name && strcmp(name, APP_RELEASE_ASSET) == 0) {
            download_url = json_object_get_string(asset, "browser_download_url");
            break;
        }
    }

    if (!download_url || download_url[0] == '\0') {
        json_value_free(json_root);
        strcpy(update_status.error_message, "Release package not found");
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    strncpy(update_status.download_url, download_url, sizeof(update_status.download_url) - 1);

    const char* body = json_object_get_string(release, "body");
    if (body) {
        strncpy(update_status.release_notes, body, sizeof(update_status.release_notes) - 1);
        update_status.release_notes[sizeof(update_status.release_notes) - 1] = '\0';
    }

    json_value_free(json_root);

    update_status.update_available = true;
    snprintf(update_status.status_message, sizeof(update_status.status_message),
        "Update available: %s", update_status.latest_version);
    update_status.progress_percent = 100;
    update_status.state = SELFUPDATE_STATE_IDLE;
    update_running = false;

    return NULL;
}


// Update thread - downloads and applies update
static void* update_thread_func(void* arg) {
    (void)arg;

    char temp_dir[512];
    if (!mk_tempdir("app_update", temp_dir, sizeof(temp_dir))) {
        strcpy(update_status.error_message, "No room to stage the update");
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    // Download the ZIP file
    update_status.state = SELFUPDATE_STATE_DOWNLOADING;
    strcpy(update_status.status_message, "Downloading update...");
    update_status.progress_percent = 0;
    update_status.download_bytes = 0;
    update_status.download_total = 0;
    strcpy(update_status.status_detail, "Connecting...");

    char zip_file[600];
    snprintf(zip_file, sizeof(zip_file), "%s/update.zip", temp_dir);

    if (update_cancel) {
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_IDLE;
        update_running = false;
        return NULL;
    }

    long total_size = wget_probe_size(update_status.download_url);

    // Fallback to ~5MB if size detection fails
    if (total_size <= 0) {
        total_size = 5 * 1024 * 1024;
    }
    update_status.download_total = total_size;

    int downloaded = wget_download_file(update_status.download_url, zip_file,
                                       report_download_progress, NULL);

    if (update_cancel) {
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_IDLE;
        update_running = false;
        return NULL;
    }

    if (downloaded < 0) {
        strcpy(update_status.error_message, "Download failed");
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    update_status.download_bytes = downloaded;
    snprintf(update_status.status_detail, sizeof(update_status.status_detail),
        "%.1f MB downloaded", downloaded / (1024.0 * 1024.0));

    update_status.progress_percent = 40;

    if (update_cancel) {
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_IDLE;
        update_running = false;
        return NULL;
    }

    // Extract the ZIP file
    update_status.state = SELFUPDATE_STATE_EXTRACTING;
    strcpy(update_status.status_message, "Extracting update...");
    strcpy(update_status.status_detail, "");  // Clear size detail for non-download phases
    update_status.progress_percent = 45;

    char extract_dir[600];
    snprintf(extract_dir, sizeof(extract_dir), "%s/extracted", temp_dir);
    mkdir(extract_dir, 0755);

    // Extract using libzip
    if (extract_zip(zip_file, extract_dir) != 0) {
        strcpy(update_status.error_message, "Extraction failed");
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    update_status.progress_percent = 60;

    // The package may nest the pak inside a wrapper directory; launch.sh marks the root
    char update_root[600];
    if (!find_file(extract_dir, "launch.sh", update_root, sizeof(update_root))) {
        strcpy(update_status.error_message, "Invalid update package");
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    char* last_slash = strrchr(update_root, '/');
    if (last_slash) *last_slash = '\0';

    update_status.progress_percent = 65;

    if (update_cancel) {
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_IDLE;
        update_running = false;
        return NULL;
    }

    // Apply update
    update_status.state = SELFUPDATE_STATE_APPLYING;
    strcpy(update_status.status_message, "Installing update...");
    update_status.progress_percent = 70;

    // Sync all files: copy everything from update, remove orphaned files
    // This handles: musicplayer.elf, launch.sh, bin/, fonts/, stations/, state/, etc.
    // Note: Linux allows replacing a running binary - it continues from memory
    if (sync_directories(update_root, pak_path) != 0) {
        strcpy(update_status.error_message, "Failed to install update");
        rm_rf(temp_dir);
        update_status.state = SELFUPDATE_STATE_ERROR;
        update_running = false;
        return NULL;
    }

    update_status.progress_percent = 90;

    // Ensure executables have correct permissions
    // Binaries are now in bin/$PLATFORM/ subdirectories
    char binary_path[600], launch_path[600];
    snprintf(binary_path, sizeof(binary_path), "%s/bin/tg5040/musicplayer.elf", pak_path);
    chmod(binary_path, 0755);
    snprintf(binary_path, sizeof(binary_path), "%s/bin/tg5050/musicplayer.elf", pak_path);
    chmod(binary_path, 0755);
    snprintf(launch_path, sizeof(launch_path), "%s/launch.sh", pak_path);
    chmod(launch_path, 0755);

    update_status.progress_percent = 95;

    // Update version file (in case state/ wasn't in the package or needs override)
    FILE* vf = fopen(version_file, "w");
    if (vf) {
        fprintf(vf, "%s\n", update_status.latest_version);
        fclose(vf);
    }

    // Sync filesystem
    sync();

    // Cleanup temp directory
    rm_rf(temp_dir);

    update_status.progress_percent = 100;
    strcpy(update_status.status_message, "Update complete!");
    update_status.state = SELFUPDATE_STATE_COMPLETED;
    update_running = false;

    return NULL;
}
