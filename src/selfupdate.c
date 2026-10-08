#include "selfupdate.h"
#include "version.h"
#include "wget_fetch.h"
#include "file_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <zip.h>

#include "include/parson/parson.h"
#include "defines.h"
#include "api.h"
#include "debug.h"
#include "settings.h"

// One release with its notes fits the first. The notes of the recent releases
// of the Beta channel do not.
#define RELEASE_JSON_MAX      32768
#define RELEASE_LIST_JSON_MAX 262144
#define RELEASE_LIST_COUNT    10

// How the progress bar is shared out. Download dominates, but unpacking and
// installing move tens of megabytes on and off the SD card and are slow enough
// that a bar frozen at one number reads as a hang.
#define EXTRACT_BASE_PCT 45
#define EXTRACT_SPAN_PCT 20
#define APPLY_BASE_PCT   70
#define APPLY_SPAN_PCT   20

// Paths
static char pak_path[512] = "";
static char current_version[32] = "";

// Update status. A worker writes the progress fields directly, and publishes
// its result under status_mutex.
static SelfUpdateStatus update_status = {0};
static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t update_thread;
static volatile bool update_running = false;
static volatile bool update_cancel = false;

// The number of the last request. A worker publishes its result only when no
// later request came, thus a change of the channel discards a running check.
static int request_id = 0;

// What the running worker answers. Set before the worker starts.
static int worker_request = 0;
static UpdateChannel worker_channel = UPDATE_CHANNEL_STABLE;

// The result fields of the status, which a worker publishes once at its end.
typedef struct {
    SelfUpdateState state;
    bool            update_available;
    char            latest_version[32];
    char            download_url[512];
    char            release_notes[1024];
    char            status_message[256];
    char            error_message[256];
} WorkerResult;

// Files the last extraction wrote out, which is what the install then copies
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




// Drives the extract slice of the progress bar from the archive's entry count.
static void note_entry_extracted(long done, long total, void* ctx) {
    (void)ctx;

    if (total > 0) {
        update_status.progress_percent = EXTRACT_BASE_PCT +
            (int)((long long)EXTRACT_SPAN_PCT * done / total);
    }

    snprintf(update_status.status_detail, sizeof(update_status.status_detail),
        "%ld / %ld files", done, total);
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

// Install the unpacked update over the pak: copy everything across, then drop
// whatever the new package no longer has.
static int sync_directories(const char* src, const char* dst) {
    int installed = 0;
    if (!cp_rf(src, dst, note_file_installed, &installed)) return -1;

    SelfUpdate_removeObsoleteFiles(src, dst);
    return 0;
}


int SelfUpdate_init(const char* path) {
    if (!path) return -1;

    strncpy(pak_path, path, sizeof(pak_path) - 1);

    snprintf(current_version, sizeof(current_version), "%s", APP_VERSION);

    memset(&update_status, 0, sizeof(update_status));
    strncpy(update_status.current_version, current_version, sizeof(update_status.current_version));

    if (Settings_getEnum(&SETTING_UPDATE_CHANNEL) != UPDATE_CHANNEL_OFF) {
        SelfUpdate_checkForUpdate();
    }

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

// Call with status_mutex held.
static void clear_result(void) {
    update_status.state = SELFUPDATE_STATE_IDLE;
    update_status.update_available = false;
    update_status.latest_version[0] = '\0';
    update_status.download_url[0] = '\0';
    update_status.release_notes[0] = '\0';
    update_status.status_message[0] = '\0';
    update_status.error_message[0] = '\0';
    update_status.progress_percent = 0;
}

// Call with status_mutex held.
static void copy_result_from_status(WorkerResult* result) {
    result->state = update_status.state;
    result->update_available = update_status.update_available;
    memcpy(result->latest_version, update_status.latest_version, sizeof(result->latest_version));
    memcpy(result->download_url, update_status.download_url, sizeof(result->download_url));
    memcpy(result->release_notes, update_status.release_notes, sizeof(result->release_notes));
    memcpy(result->status_message, update_status.status_message, sizeof(result->status_message));
    memcpy(result->error_message, update_status.error_message, sizeof(result->error_message));
}

// Publishes the result of a worker and ends it. A result of an earlier request
// is discarded, except a completed install: its files are on the card already.
static void finish_worker(const WorkerResult* result, int request) {
    pthread_mutex_lock(&status_mutex);
    if (request == request_id || result->state == SELFUPDATE_STATE_COMPLETED) {
        update_status.state = result->state;
        update_status.update_available = result->update_available;
        memcpy(update_status.latest_version, result->latest_version,
               sizeof(update_status.latest_version));
        memcpy(update_status.download_url, result->download_url,
               sizeof(update_status.download_url));
        memcpy(update_status.release_notes, result->release_notes,
               sizeof(update_status.release_notes));
        memcpy(update_status.status_message, result->status_message,
               sizeof(update_status.status_message));
        memcpy(update_status.error_message, result->error_message,
               sizeof(update_status.error_message));
    } else {
        clear_result();
    }
    update_running = false;
    pthread_mutex_unlock(&status_mutex);
}

// Call with status_mutex held. Returns false when a worker runs or the thread
// cannot start.
static bool start_worker(void* (*worker)(void*), UpdateChannel channel) {
    if (update_running) return false;

    update_cancel = false;
    update_running = true;
    worker_request = ++request_id;
    worker_channel = channel;

    if (pthread_create(&update_thread, NULL, worker, NULL) != 0) {
        update_running = false;
        return false;
    }
    return true;
}

int SelfUpdate_checkForUpdate(void) {
    UpdateChannel channel = Settings_getEnum(&SETTING_UPDATE_CHANNEL);
    if (channel == UPDATE_CHANNEL_OFF) channel = UPDATE_CHANNEL_STABLE;

    pthread_mutex_lock(&status_mutex);
    if (update_running) {
        pthread_mutex_unlock(&status_mutex);
        return -1;
    }

    memset(&update_status, 0, sizeof(update_status));
    update_status.state = SELFUPDATE_STATE_CHECKING;
    strncpy(update_status.current_version, current_version, sizeof(update_status.current_version));
    strcpy(update_status.status_message, "Checking for updates...");

    bool started = start_worker(check_thread_func, channel);
    if (!started) {
        update_status.state = SELFUPDATE_STATE_ERROR;
        strcpy(update_status.error_message, "Failed to start update check");
    }
    pthread_mutex_unlock(&status_mutex);
    return started ? 0 : -1;
}

int SelfUpdate_startUpdate(void) {
    pthread_mutex_lock(&status_mutex);
    if (update_running || !update_status.update_available ||
        update_status.state == SELFUPDATE_STATE_COMPLETED) {
        pthread_mutex_unlock(&status_mutex);
        return -1;
    }

    update_status.state = SELFUPDATE_STATE_DOWNLOADING;
    update_status.progress_percent = 0;
    strcpy(update_status.status_message, "Starting download...");

    bool started = start_worker(update_thread_func, worker_channel);
    if (!started) {
        update_status.state = SELFUPDATE_STATE_ERROR;
        strcpy(update_status.error_message, "Failed to start update");
    }
    pthread_mutex_unlock(&status_mutex);
    return started ? 0 : -1;
}

void SelfUpdate_cancelUpdate(void) {
    if (update_running) {
        update_cancel = true;
    }
}

void SelfUpdate_forgetCheck(void) {
    pthread_mutex_lock(&status_mutex);
    request_id++;
    if (!update_running && update_status.state != SELFUPDATE_STATE_COMPLETED) {
        clear_result();
    }
    pthread_mutex_unlock(&status_mutex);
}

SelfUpdateStatus SelfUpdate_getStatus(void) {
    pthread_mutex_lock(&status_mutex);
    SelfUpdateStatus status = update_status;
    pthread_mutex_unlock(&status_mutex);
    return status;
}

UpdateUiState SelfUpdate_uiState(const SelfUpdateStatus* status) {
    if (!status) return UPDATE_UI_UNCHECKED;

    if (status->state == SELFUPDATE_STATE_COMPLETED) return UPDATE_UI_RESTART;
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
    return SelfUpdate_getState() == SELFUPDATE_STATE_COMPLETED;
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
    pthread_mutex_lock(&status_mutex);
    SelfUpdateState state = update_status.state;
    pthread_mutex_unlock(&status_mutex);
    return state;
}

static void fail(WorkerResult* result, const char* message) {
    result->state = SELFUPDATE_STATE_ERROR;
    snprintf(result->error_message, sizeof(result->error_message), "%s", message);
}

// Fills result with the release that the channel selects. Leaves the state IDLE
// with no version after a cancel.
static void run_check(UpdateChannel channel, WorkerResult* result) {
    int conn = system("ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1");
    if (conn != 0) {
        conn = system("ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1");
    }
    if (conn != 0) {
        fail(result, "No internet connection");
        return;
    }
    if (update_cancel) return;

    update_status.progress_percent = 20;

    bool beta = channel == UPDATE_CHANNEL_BETA;
    char api_url[256];
    if (beta) {
        snprintf(api_url, sizeof(api_url), "https://api.github.com/repos/%s/releases?per_page=%d",
                 APP_GITHUB_REPO, RELEASE_LIST_COUNT);
    } else {
        snprintf(api_url, sizeof(api_url), "https://api.github.com/repos/%s/releases/latest",
                 APP_GITHUB_REPO);
    }

    char* release_json = fetch_to_memory(api_url, beta ? RELEASE_LIST_JSON_MAX : RELEASE_JSON_MAX);
    if (!release_json) {
        fail(result, "Failed to check GitHub");
        return;
    }
    if (update_cancel) {
        free(release_json);
        return;
    }

    update_status.progress_percent = 50;

    JSON_Value* json_root = json_parse_string(release_json);
    free(release_json);

    VersionReleaseStatus found = Version_bestReleaseExists(json_root, APP_RELEASE_ASSET);
    if (found != VERSION_RELEASE_FOUND) {
        json_value_free(json_root);
        fail(result, found == VERSION_RELEASE_NO_ASSET ? "Release package not found"
                                                       : "Could not parse version");
        return;
    }

    const JSON_Object* release = Version_getBestRelease(json_root, APP_RELEASE_ASSET);
    const char* tag = json_object_get_string(release, "tag_name");
    // A tag longer than the field is cut for display only.
    snprintf(result->latest_version, sizeof(result->latest_version), "%s", tag);

    update_status.progress_percent = 70;

    if (Version_compare(tag, current_version) <= 0) {
        json_value_free(json_root);
        strcpy(result->status_message, "Already up to date");
        return;
    }

    snprintf(result->download_url, sizeof(result->download_url), "%s",
             Version_getAssetUrl(release, APP_RELEASE_ASSET));

    const char* body = json_object_get_string(release, "body");
    if (body) {
        snprintf(result->release_notes, sizeof(result->release_notes), "%s", body);
    }

    json_value_free(json_root);

    result->update_available = true;
    snprintf(result->status_message, sizeof(result->status_message),
        "Update available: %s", result->latest_version);
    update_status.progress_percent = 100;
}

static void* check_thread_func(void* arg) {
    (void)arg;

    int request = worker_request;
    WorkerResult result = {
        .state = SELFUPDATE_STATE_IDLE,
    };
    run_check(worker_channel, &result);
    finish_worker(&result, request);
    return NULL;
}

// Shows the phase of the install. The About screen and the update screen read
// the state and the message as one snapshot.
static void set_install_phase(SelfUpdateState state, const char* message) {
    pthread_mutex_lock(&status_mutex);
    update_status.state = state;
    snprintf(update_status.status_message, sizeof(update_status.status_message), "%s", message);
    pthread_mutex_unlock(&status_mutex);
}

// Downloads, unpacks and installs the release of the last check. Sets the state
// of result to IDLE after a cancel, COMPLETED after the install, or ERROR.
static void run_install(WorkerResult* result, const char* temp_dir) {
    // Download the ZIP file
    set_install_phase(SELFUPDATE_STATE_DOWNLOADING, "Downloading update...");
    update_status.progress_percent = 0;
    update_status.download_bytes = 0;
    update_status.download_total = 0;
    strcpy(update_status.status_detail, "Connecting...");

    char zip_file[600];
    snprintf(zip_file, sizeof(zip_file), "%s/update.zip", temp_dir);

    if (update_cancel) {
        result->state = SELFUPDATE_STATE_IDLE;
        return;
    }

    long total_size = wget_probe_size(result->download_url);

    // Fallback to ~5MB if size detection fails
    if (total_size <= 0) {
        total_size = 5 * 1024 * 1024;
    }
    update_status.download_total = total_size;

    int downloaded = wget_download_file(result->download_url, zip_file,
                                       report_download_progress, NULL);

    if (update_cancel) {
        result->state = SELFUPDATE_STATE_IDLE;
        return;
    }

    if (downloaded < 0) {
        fail(result, "Download failed");
        return;
    }

    update_status.download_bytes = downloaded;
    snprintf(update_status.status_detail, sizeof(update_status.status_detail),
        "%.1f MB downloaded", downloaded / (1024.0 * 1024.0));

    update_status.progress_percent = 40;

    if (update_cancel) {
        result->state = SELFUPDATE_STATE_IDLE;
        return;
    }

    // Extract the ZIP file
    set_install_phase(SELFUPDATE_STATE_EXTRACTING, "Extracting update...");
    strcpy(update_status.status_detail, "");  // Clear size detail for non-download phases
    update_status.progress_percent = 45;

    char extract_dir[600];
    snprintf(extract_dir, sizeof(extract_dir), "%s/extracted", temp_dir);
    mkdir(extract_dir, 0755);

    // Extract using libzip
    extracted_files = extract_zip(zip_file, extract_dir, note_entry_extracted, NULL);
    if (extracted_files < 0) {
        fail(result, "Extraction failed");
        return;
    }

    update_status.progress_percent = 60;

    // The package may nest the pak inside a wrapper directory; launch.sh marks the root
    char update_root[600];
    if (!find_file(extract_dir, "launch.sh", update_root, sizeof(update_root))) {
        fail(result, "Invalid update package");
        return;
    }

    char* last_slash = strrchr(update_root, '/');
    if (last_slash) *last_slash = '\0';

    update_status.progress_percent = 65;

    if (update_cancel) {
        result->state = SELFUPDATE_STATE_IDLE;
        return;
    }

    // Apply update
    set_install_phase(SELFUPDATE_STATE_APPLYING, "Installing update...");
    update_status.progress_percent = 70;

    // Sync all files: copy everything from update, remove obsolete files
    // This handles: musicplayer.elf, launch.sh, bin/, fonts/, stations/, state/, etc.
    // Note: Linux allows replacing a running binary - it continues from memory
    if (sync_directories(update_root, pak_path) != 0) {
        fail(result, "Failed to install update");
        return;
    }

    update_status.progress_percent = 95;

    // Sync filesystem
    sync();

    update_status.progress_percent = 100;
    strcpy(result->status_message, "Update complete!");
    result->state = SELFUPDATE_STATE_COMPLETED;
}

static void* update_thread_func(void* arg) {
    (void)arg;

    int request = worker_request;
    WorkerResult result;
    pthread_mutex_lock(&status_mutex);
    copy_result_from_status(&result);
    pthread_mutex_unlock(&status_mutex);
    result.error_message[0] = '\0';

    char temp_dir[512];
    if (!mk_tempdir("app_update", temp_dir, sizeof(temp_dir))) {
        fail(&result, "No room to stage the update");
    } else {
        run_install(&result, temp_dir);
        rm_rf(temp_dir);
    }

    finish_worker(&result, request);
    return NULL;
}
