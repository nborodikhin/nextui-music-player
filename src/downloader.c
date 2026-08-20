#define _GNU_SOURCE
#include "downloader.h"
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <ctype.h>

#include "defines.h"
#include "api.h"

// Identifies the app to GitHub on every fetch
#define HTTP_USER_AGENT "NextUI-Music-Player"

// Paths
static char ytdlp_path[512] = "";
static char qjs_path[512] = "";
static char ffmpeg_path[512] = "";
static char download_dir[512] = "";
static char queue_file[512] = "";
static char version_file[512] = "";
static char pak_path[512] = "";

// Module state
static bool youtube_initialized = false;
static DownloaderState youtube_state = DOWNLOADER_STATE_IDLE;
static char error_message[256] = "";

// Download queue
static DownloaderQueueItem download_queue[DOWNLOADER_MAX_QUEUE];
static int queue_count = 0;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

// Download status
static DownloaderDownloadStatus download_status = {0};
static pthread_t download_thread;
static volatile bool download_running = false;
static volatile bool download_should_stop = false;

// Update status
static DownloaderUpdateStatus update_status = {0};
static pthread_t update_thread;
static volatile bool update_running = false;
static volatile bool update_should_stop = false;

// Search
static pthread_t search_thread;
static volatile bool search_running = false;
static volatile bool search_should_stop = false;
static DownloaderResult search_results[DOWNLOADER_MAX_RESULTS];
static int search_result_count = 0;
static DownloaderSearchStatus search_status = {0};
static char search_query_copy[256] = "";
static int search_max_results = DOWNLOADER_MAX_RESULTS;

// Current yt-dlp version
static char current_version[32] = "unknown";
static bool paths_ready = false;

// Forward declarations
static bool ytdlp_present(void);
static bool ffmpeg_present(void);
static void* download_thread_func(void* arg);
static void* update_thread_func(void* arg);
static void* search_thread_func(void* arg);
static int run_command(const char* cmd, char* output, size_t output_size);
static void sanitize_filename(const char* input, char* output, size_t max_len);
static void clean_title(char* title);

// Clean title by removing text inside () and [] brackets
static void clean_title(char* title) {
    if (!title || !title[0]) return;

    char result[512];
    int j = 0;
    int paren_depth = 0;   // Track nested ()
    int bracket_depth = 0; // Track nested []

    for (int i = 0; title[i] && j < (int)sizeof(result) - 1; i++) {
        char c = title[i];

        if (c == '(') {
            paren_depth++;
        } else if (c == ')') {
            if (paren_depth > 0) paren_depth--;
        } else if (c == '[') {
            bracket_depth++;
        } else if (c == ']') {
            if (bracket_depth > 0) bracket_depth--;
        } else if (paren_depth == 0 && bracket_depth == 0) {
            result[j++] = c;
        }
    }
    result[j] = '\0';

    // Trim trailing spaces
    while (j > 0 && result[j-1] == ' ') {
        result[--j] = '\0';
    }

    // Trim leading spaces
    char* start = result;
    while (*start == ' ') start++;

    // Copy back to title (title buffer is at least 512 bytes from caller)
    strncpy(title, start, 511);
    title[511] = '\0';
}

// Set up paths and directories that do not depend on the binary being present.
// Safe to call repeatedly; only the first call does the work.
static void ensure_paths(void) {
    if (paths_ready) return;

    // Pak directory is current working directory (launch.sh sets cwd to pak folder)
    strncpy(pak_path, ".", sizeof(pak_path) - 1);
    pak_path[sizeof(pak_path) - 1] = '\0';

    snprintf(ytdlp_path, sizeof(ytdlp_path), "%s/bin/yt-dlp", pak_path);
    snprintf(qjs_path, sizeof(qjs_path), "%s/bin/qjs", pak_path);
    snprintf(ffmpeg_path, sizeof(ffmpeg_path), "%s/bin/ffmpeg", pak_path);
    snprintf(version_file, sizeof(version_file), "%s/state/yt-dlp_version.txt", pak_path);
    snprintf(queue_file, sizeof(queue_file), "%s/state/youtube_queue.txt", pak_path);
    snprintf(download_dir, sizeof(download_dir), "%s/Music/Downloaded", SDCARD_PATH);

    Keyboard_init();

    char music_dir[512];
    snprintf(music_dir, sizeof(music_dir), "%s/Music", SDCARD_PATH);
    mkdir(music_dir, 0755);
    mkdir(download_dir, 0755);

    Downloader_loadQueue();

    paths_ready = true;
}

void Downloader_refreshVersion(void) {
    ensure_paths();

    // Without a binary there is no version to report, whatever the version
    // file claims - it outlives the binary it describes.
    if (!ytdlp_present()) {
        strncpy(current_version, DOWNLOADER_VERSION_NOT_INSTALLED, sizeof(current_version) - 1);
        current_version[sizeof(current_version) - 1] = '\0';
        unlink(version_file);
        return;
    }

    current_version[0] = '\0';

    FILE* f = fopen(version_file, "r");
    if (f) {
        if (fgets(current_version, sizeof(current_version), f)) {
            char* nl = strchr(current_version, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);
    }

    // No cached version, or one left behind by a binary that is no longer
    // there: ask the binary itself and cache the answer.
    if (current_version[0] == '\0' ||
        strcmp(current_version, "unknown") == 0 ||
        strcmp(current_version, DOWNLOADER_VERSION_NOT_INSTALLED) == 0) {
        strncpy(current_version, "unknown", sizeof(current_version) - 1);
        current_version[sizeof(current_version) - 1] = '\0';

        char cmd[600];
        snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", ytdlp_path);
        FILE* pipe = popen(cmd, "r");
        if (pipe) {
            char probed[32] = "";
            if (fgets(probed, sizeof(probed), pipe)) {
                char* nl = strchr(probed, '\n');
                if (nl) *nl = '\0';
            }
            pclose(pipe);

            if (probed[0] != '\0') {
                strncpy(current_version, probed, sizeof(current_version) - 1);
                current_version[sizeof(current_version) - 1] = '\0';
                FILE* vf = fopen(version_file, "w");
                if (vf) {
                    fprintf(vf, "%s\n", current_version);
                    fclose(vf);
                }
            }
        }
    }
}

int Downloader_init(void) {
    ensure_paths();
    Downloader_refreshVersion();

    // The binary is installed on demand, so its absence is an ordinary state:
    // paths stay valid and a later install can finish initialization without
    // restarting the app.
    if (!Downloader_isAvailable()) {
        strncpy(error_message, "Youtube download helpers not installed", sizeof(error_message) - 1);
        error_message[sizeof(error_message) - 1] = '\0';
        return -1;
    }

    if (youtube_initialized) return 0;

    error_message[0] = '\0';
    chmod(ytdlp_path, 0755);

    // Auto-resume pending downloads if queue has items and network is available
    if (queue_count > 0 && Downloader_checkNetwork()) {
        Downloader_downloadStart();
    }

    youtube_initialized = true;
    return 0;
}

void Downloader_cleanup(void) {
    // Stop any running operations
    Downloader_downloadStop();
    Downloader_cancelUpdate();
    Downloader_cancelSearch();

    // Wait briefly for download thread to finish
    for (int i = 0; i < 30 && download_running; i++) {
        usleep(100000);  // 100ms
    }

    // Re-enable auto sleep
    PWR_enableAutosleep();

    // Save queue (DOWNLOADING items will become PENDING on next load)
    Downloader_saveQueue();
}

// yt-dlp alone; the version file describes this binary
static bool ytdlp_present(void) {
    return access(ytdlp_path, X_OK) == 0;
}

// The JS interpreter is only ever ours: nothing in the firmware provides it
static bool qjs_present(void) {
    return access(qjs_path, X_OK) == 0;
}

// Where the firmware keeps ffmpeg, "" once we have looked and found none.
// The lookup costs a fork and the answer cannot change under us, so it is
// resolved once - this is reached from menu rendering.
static char system_ffmpeg[512] = "";
static bool system_ffmpeg_resolved = false;

// The ffmpeg yt-dlp should run: ours when installed, else the firmware's.
// Empty when there is none. Our copy is checked first and costs no fork, so an
// install is picked up immediately without invalidating anything.
static const char* ffmpeg_command(void) {
    if (access(ffmpeg_path, X_OK) == 0) return ffmpeg_path;

    if (!system_ffmpeg_resolved) {
        system_ffmpeg_resolved = true;
        FILE* pipe = popen("command -v ffmpeg 2>/dev/null", "r");
        if (pipe) {
            if (fgets(system_ffmpeg, sizeof(system_ffmpeg), pipe)) {
                char* nl = strchr(system_ffmpeg, '\n');
                if (nl) *nl = '\0';
            }
            pclose(pipe);
        }
    }
    return system_ffmpeg;
}

// The firmware normally provides ffmpeg; ours is only a fallback for images
// that do not. Returns true when either is usable.
static bool ffmpeg_present(void) {
    return ffmpeg_command()[0] != '\0';
}

// yt-dlp needs an interpreter to solve YouTube's JS challenges and ffmpeg to
// repackage and tag what it downloads, so all three must be in place.
bool Downloader_isAvailable(void) {
    return ytdlp_present() && qjs_present() && ffmpeg_present();
}

bool Downloader_checkNetwork(void) {
    // Quick connectivity check - try primary DNS first, then fallback
    int conn = system("ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1");
    if (conn != 0) {
        conn = system("ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1");
    }
    return (conn == 0);
}

const char* Downloader_getVersion(void) {
    return current_version;
}

void Downloader_cancelSearch(void) {
    search_should_stop = true;
    if (search_running) {
        // Kill any running yt-dlp search process to allow immediate re-search
        system("pkill -f 'yt-dlp.*music.youtube.com/search' 2>/dev/null");
        search_running = false;
    }
}

// Background search thread function
static void* search_thread_func(void* arg) {
    (void)arg;
    PWR_pinToCores(CPU_CORE_EFFICIENCY);

    search_status.searching = true;
    search_status.completed = false;
    search_status.result_count = 0;
    search_status.error_message[0] = '\0';

    // Check connectivity first to fail fast
    int conn = system("ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1");
    if (conn != 0) {
        conn = system("ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1");
    }

    if (conn != 0) {
        strncpy(search_status.error_message, "No internet connection", sizeof(search_status.error_message) - 1);
        search_status.error_message[sizeof(search_status.error_message) - 1] = '\0';
        search_status.result_count = -1;
        search_status.searching = false;
        search_status.completed = true;
        search_running = false;
        youtube_state = DOWNLOADER_STATE_IDLE;
        return NULL;
    }

    if (search_should_stop) {
        search_status.searching = false;
        search_status.completed = true;
        search_running = false;
        youtube_state = DOWNLOADER_STATE_IDLE;
        return NULL;
    }

    // Sanitize query - escape special characters
    char safe_query[256];
    int j = 0;
    for (int i = 0; search_query_copy[i] && j < (int)sizeof(safe_query) - 2; i++) {
        char c = search_query_copy[i];
        // Skip potentially dangerous characters for shell
        if (c == '"' || c == '\'' || c == '`' || c == '$' || c == '\\' || c == ';' || c == '&' || c == '|') {
            continue;
        }
        safe_query[j++] = c;
    }
    safe_query[j] = '\0';

    int num_results = search_max_results > DOWNLOADER_MAX_RESULTS ? DOWNLOADER_MAX_RESULTS : search_max_results;

    // Use a temp file to capture results (more reliable than pipe)
    const char* temp_file = "/tmp/yt_search_results.txt";
    const char* temp_err = "/tmp/yt_search_error.txt";

    // Build yt-dlp search command
    // Note: --socket-timeout handles network-level timeouts
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "%s 'https://music.youtube.com/search?q=%s#songs' "
        "--flat-playlist "
        "-I :%d "
        "--no-warnings "
        "--socket-timeout 15 "
        "--print '%%(id)s\t%%(title)s' "
        "> %s 2> %s",
        ytdlp_path,
        safe_query,
        num_results,
        temp_file,
        temp_err);

    int ret = system(cmd);

    // Check if cancelled during search
    if (search_should_stop) {
        unlink(temp_file);
        unlink(temp_err);
        search_status.searching = false;
        search_status.completed = true;
        search_running = false;
        youtube_state = DOWNLOADER_STATE_IDLE;
        return NULL;
    }

    if (ret != 0) {
        // Try to read error message
        FILE* err = fopen(temp_err, "r");
        if (err) {
            char err_line[256];
            if (fgets(err_line, sizeof(err_line), err)) {
                // Remove newline
                char* nl = strchr(err_line, '\n');
                if (nl) *nl = '\0';
                // Check for common errors
                if (strstr(err_line, "name resolution") || strstr(err_line, "resolve")) {
                    strncpy(search_status.error_message, "Network error - check WiFi", sizeof(search_status.error_message) - 1);
                } else if (strstr(err_line, "timed out") || strstr(err_line, "timeout")) {
                    strncpy(search_status.error_message, "Connection timed out", sizeof(search_status.error_message) - 1);
                } else {
                    strncpy(search_status.error_message, "Search failed", sizeof(search_status.error_message) - 1);
                }
                search_status.error_message[sizeof(search_status.error_message) - 1] = '\0';
                LOG_error("yt-dlp error: %s\n", err_line);
            }
            fclose(err);
        }
    }

    // Read results from temp file
    FILE* f = fopen(temp_file, "r");
    if (!f) {
        if (search_status.error_message[0] == '\0') {
            strncpy(search_status.error_message, "Failed to read search results", sizeof(search_status.error_message) - 1);
            search_status.error_message[sizeof(search_status.error_message) - 1] = '\0';
        }
        search_status.result_count = -1;
        search_status.searching = false;
        search_status.completed = true;
        search_running = false;
        youtube_state = DOWNLOADER_STATE_IDLE;
        return NULL;
    }

    char line[512];
    int count = 0;

    while (fgets(line, sizeof(line), f) && count < search_max_results) {
        // Check for cancellation
        if (search_should_stop) {
            break;
        }

        // Remove newline
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Skip empty lines
        if (line[0] == '\0') continue;

        // Make a copy for strtok since it modifies the string
        char line_copy[512];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';

        // Parse: id<TAB>title (tab-separated)
        char* id = strtok(line_copy, "\t");
        char* title = strtok(NULL, "\t");

        if (id && title && strlen(id) > 0) {
            strncpy(search_results[count].title, title, DOWNLOADER_MAX_TITLE - 1);
            search_results[count].title[DOWNLOADER_MAX_TITLE - 1] = '\0';

            strncpy(search_results[count].video_id, id, DOWNLOADER_VIDEO_ID_LEN - 1);
            search_results[count].video_id[DOWNLOADER_VIDEO_ID_LEN - 1] = '\0';

            search_results[count].artist[0] = '\0';
            search_results[count].duration_sec = 0;

            count++;
        }
    }

    fclose(f);

    // Cleanup temp files
    unlink(temp_file);
    unlink(temp_err);

    search_status.result_count = count;
    search_status.searching = false;
    search_status.completed = true;
    search_running = false;
    youtube_state = DOWNLOADER_STATE_IDLE;

    return NULL;
}

// Start async search
int Downloader_startSearch(const char* query) {
    if (!query || search_running) {
        return -1;
    }

    // Reset status
    memset(&search_status, 0, sizeof(search_status));
    search_result_count = 0;

    // Copy query for thread
    strncpy(search_query_copy, query, sizeof(search_query_copy) - 1);
    search_query_copy[sizeof(search_query_copy) - 1] = '\0';

    search_running = true;
    search_should_stop = false;
    youtube_state = DOWNLOADER_STATE_SEARCHING;

    if (pthread_create(&search_thread, NULL, search_thread_func, NULL) != 0) {
        search_running = false;
        youtube_state = DOWNLOADER_STATE_ERROR;
        strncpy(search_status.error_message, "Failed to start search", sizeof(search_status.error_message) - 1);
        search_status.error_message[sizeof(search_status.error_message) - 1] = '\0';
        search_status.result_count = -1;
        search_status.completed = true;
        return -1;
    }

    pthread_detach(search_thread);
    return 0;
}

// Get search status
const DownloaderSearchStatus* Downloader_getSearchStatus(void) {
    return &search_status;
}

// Get search results
DownloaderResult* Downloader_getSearchResults(void) {
    return search_results;
}

int Downloader_queueAdd(const char* video_id, const char* title) {
    if (!video_id || !title) return -1;

    pthread_mutex_lock(&queue_mutex);

    // Check if already in queue
    for (int i = 0; i < queue_count; i++) {
        if (strcmp(download_queue[i].video_id, video_id) == 0) {
            pthread_mutex_unlock(&queue_mutex);
            return 0;  // Already in queue
        }
    }

    // Check queue size
    if (queue_count >= DOWNLOADER_MAX_QUEUE) {
        pthread_mutex_unlock(&queue_mutex);
        return -1;  // Queue full
    }

    // Add to queue
    strncpy(download_queue[queue_count].video_id, video_id, DOWNLOADER_VIDEO_ID_LEN - 1);
    strncpy(download_queue[queue_count].title, title, DOWNLOADER_MAX_TITLE - 1);
    download_queue[queue_count].status = DOWNLOADER_STATUS_PENDING;
    download_queue[queue_count].progress_percent = 0;
    queue_count++;

    pthread_mutex_unlock(&queue_mutex);

    // Save queue to file
    Downloader_saveQueue();

    // Auto-start download thread if not already running
    Downloader_downloadStart();

    return 1;  // Successfully added
}

int Downloader_queueRemove(int index) {
    pthread_mutex_lock(&queue_mutex);

    if (index < 0 || index >= queue_count) {
        pthread_mutex_unlock(&queue_mutex);
        return -1;
    }

    // Shift items
    for (int i = index; i < queue_count - 1; i++) {
        download_queue[i] = download_queue[i + 1];
    }
    queue_count--;

    pthread_mutex_unlock(&queue_mutex);

    Downloader_saveQueue();
    return 0;
}

int Downloader_queueRemoveById(const char* video_id) {
    if (!video_id) return -1;

    pthread_mutex_lock(&queue_mutex);

    int found_index = -1;
    for (int i = 0; i < queue_count; i++) {
        if (strcmp(download_queue[i].video_id, video_id) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index < 0) {
        pthread_mutex_unlock(&queue_mutex);
        return -1;  // Not found
    }

    // Shift items
    for (int i = found_index; i < queue_count - 1; i++) {
        download_queue[i] = download_queue[i + 1];
    }
    queue_count--;

    pthread_mutex_unlock(&queue_mutex);

    Downloader_saveQueue();
    return 0;
}

int Downloader_queueClear(void) {
    pthread_mutex_lock(&queue_mutex);
    queue_count = 0;
    pthread_mutex_unlock(&queue_mutex);

    Downloader_saveQueue();
    return 0;
}

int Downloader_queueCount(void) {
    return queue_count;
}

DownloaderQueueItem* Downloader_queueGet(int* count) {
    if (count) *count = queue_count;
    return download_queue;
}

bool Downloader_isInQueue(const char* video_id) {
    if (!video_id) return false;

    pthread_mutex_lock(&queue_mutex);
    for (int i = 0; i < queue_count; i++) {
        if (strcmp(download_queue[i].video_id, video_id) == 0) {
            pthread_mutex_unlock(&queue_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&queue_mutex);
    return false;
}

bool Downloader_isDownloaded(const char* video_id) {
    if (!video_id) return false;

    // Check if file exists in download directory
    // This is a simple check - could be improved with a database
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s/*%s*", download_dir, video_id);

    // For now, just return false - would need glob() for proper implementation
    return false;
}

// Parse yt-dlp speed string like "1.23MiB/s" or "500KiB/s" to bytes/sec
static int parse_ytdlp_speed(const char* speed_str) {
    if (!speed_str) return 0;
    float val = 0;
    if (sscanf(speed_str, "%f", &val) != 1) return 0;
    if (strstr(speed_str, "GiB/s")) return (int)(val * 1024 * 1024 * 1024);
    if (strstr(speed_str, "MiB/s")) return (int)(val * 1024 * 1024);
    if (strstr(speed_str, "KiB/s")) return (int)(val * 1024);
    if (strstr(speed_str, "B/s"))   return (int)val;
    return 0;
}

// Parse yt-dlp ETA string like "00:03" or "01:23:45" to seconds
static int parse_ytdlp_eta(const char* eta_str) {
    if (!eta_str) return 0;
    int h = 0, m = 0, s = 0;
    // Try HH:MM:SS first
    if (sscanf(eta_str, "%d:%d:%d", &h, &m, &s) == 3) {
        return h * 3600 + m * 60 + s;
    }
    // Try MM:SS
    if (sscanf(eta_str, "%d:%d", &m, &s) == 2) {
        return m * 60 + s;
    }
    return 0;
}

static void* download_thread_func(void* arg) {
    (void)arg;
    PWR_pinToCores(CPU_CORE_EFFICIENCY);

    // Disable auto sleep while downloading
    PWR_disableAutosleep();

    while (!download_should_stop) {
        pthread_mutex_lock(&queue_mutex);

        // Find next pending item
        int download_index = -1;
        for (int i = 0; i < queue_count; i++) {
            if (download_queue[i].status == DOWNLOADER_STATUS_PENDING) {
                download_index = i;
                break;
            }
        }

        if (download_index < 0) {
            pthread_mutex_unlock(&queue_mutex);
            break;  // No more items
        }

        // Mark as downloading
        download_queue[download_index].status = DOWNLOADER_STATUS_DOWNLOADING;
        char video_id[DOWNLOADER_VIDEO_ID_LEN];
        char title[DOWNLOADER_MAX_TITLE];
        strncpy(video_id, download_queue[download_index].video_id, sizeof(video_id));
        strncpy(title, download_queue[download_index].title, sizeof(title));

        pthread_mutex_unlock(&queue_mutex);

        // Update status
        download_status.current_index = download_index;
        strncpy(download_status.current_title, title, sizeof(download_status.current_title));

        // Sanitize filename
        char safe_filename[128];
        sanitize_filename(title, safe_filename, sizeof(safe_filename));

        char output_file[600];
        char temp_file[600];
        snprintf(output_file, sizeof(output_file), "%s/%s.m4a", download_dir, safe_filename);
        snprintf(temp_file, sizeof(temp_file), "%s/.downloading_%s.m4a", download_dir, video_id);

        // Check if already exists
        bool success = false;
        if (access(output_file, F_OK) == 0) {
            success = true;
        } else {
            // Build download command - audio-only itag 140 (AAC-LC in m4a),
            // so nothing is ever merged or re-encoded. yt-dlp still shells out
            // to ffmpeg for two stream-copy passes: repackaging the DASH
            // container and writing the tags.
            // Album art will be fetched by player during playback
            // Force M4A only - no fallback to other formats
            // socket-timeout prevents network hangs
            char cmd[2048];
            snprintf(cmd, sizeof(cmd),
                "%s "
                "-f \"bestaudio[ext=m4a]\" "
                "--js-runtimes \"quickjs:%s\" "
                "--ffmpeg-location \"%s\" "
                "--embed-metadata "
                "--socket-timeout 30 "
                "--parse-metadata \"title:%%(artist)s - %%(title)s\" "
                "--newline --progress "
                "-o \"%s\" "
                "--no-playlist "
                "\"https://music.youtube.com/watch?v=%s\" "
                "2>&1",
                ytdlp_path, qjs_path, ffmpeg_command(), temp_file, video_id);


            // Use popen to read progress in real-time
            FILE* pipe = popen(cmd, "r");
            int result = -1;

            if (pipe) {
                char line[512];
                while (fgets(line, sizeof(line), pipe)) {
                    // Log errors from yt-dlp
                    if (strstr(line, "ERROR") || strstr(line, "error:")) {
                        LOG_error("yt-dlp: %s", line);
                    }

                    // Parse progress from yt-dlp output
                    // Format: [download]  55.3% of ~  5.21MiB at  1.23MiB/s ETA 00:03
                    if (strstr(line, "[download]")) {
                        char* pct = strstr(line, "%");
                        if (pct) {
                            // Find the start of the percentage number
                            char* start = pct - 1;
                            while (start > line && (isdigit(*start) || *start == '.')) {
                                start--;
                            }
                            start++;

                            float percent = 0;
                            if (sscanf(start, "%f", &percent) == 1) {
                                int speed = 0;
                                int eta = 0;

                                // Parse speed: find "at" keyword then speed value
                                char* at_ptr = strstr(line, " at ");
                                if (at_ptr) {
                                    speed = parse_ytdlp_speed(at_ptr + 4);
                                }

                                // Parse ETA: find "ETA" keyword
                                char* eta_ptr = strstr(line, "ETA ");
                                if (eta_ptr) {
                                    eta = parse_ytdlp_eta(eta_ptr + 4);
                                }

                                pthread_mutex_lock(&queue_mutex);
                                if (download_index < queue_count) {
                                    // Download is ~80% of total, post-processing is ~20%
                                    download_queue[download_index].progress_percent = (int)(percent * 0.8f);
                                    download_queue[download_index].speed_bps = speed;
                                    download_queue[download_index].eta_sec = eta;
                                    download_status.speed_bps = speed;
                                    download_status.eta_sec = eta;
                                }
                                pthread_mutex_unlock(&queue_mutex);
                            }
                        }
                    }
                    // Check for post-processing progress (metadata/thumbnail embedding)
                    if (strstr(line, "[EmbedThumbnail]") || strstr(line, "Post-process")) {
                        pthread_mutex_lock(&queue_mutex);
                        if (download_index < queue_count) {
                            download_queue[download_index].progress_percent = 85;
                            download_queue[download_index].speed_bps = 0;
                            download_queue[download_index].eta_sec = 0;
                            download_status.speed_bps = 0;
                            download_status.eta_sec = 0;
                        }
                        pthread_mutex_unlock(&queue_mutex);
                    }
                    if (strstr(line, "[Metadata]") || strstr(line, "Adding metadata")) {
                        pthread_mutex_lock(&queue_mutex);
                        if (download_index < queue_count) {
                            download_queue[download_index].progress_percent = 95;
                            download_queue[download_index].speed_bps = 0;
                            download_queue[download_index].eta_sec = 0;
                        }
                        pthread_mutex_unlock(&queue_mutex);
                    }
                }
                result = pclose(pipe);
            }

            if (result == 0 && access(temp_file, F_OK) == 0) {
                // Validate M4A file before moving
                bool valid_m4a = false;
                struct stat st;
                if (stat(temp_file, &st) == 0 && st.st_size >= 10240) {
                    // Minimum 10KB for a valid M4A
                    int fd = open(temp_file, O_RDONLY);
                    if (fd >= 0) {
                        unsigned char header[12];
                        if (read(fd, header, 12) == 12) {
                            // Check for ftyp atom (MP4/M4A container)
                            // Bytes 4-7 should be "ftyp"
                            if (header[4] == 'f' && header[5] == 't' &&
                                header[6] == 'y' && header[7] == 'p') {
                                valid_m4a = true;
                            }
                        }
                        close(fd);
                    }
                }

                if (valid_m4a) {
                    // Sync file to disk before rename
                    int fd = open(temp_file, O_RDONLY);
                    if (fd >= 0) {
                        fsync(fd);
                        close(fd);
                    }
                    // Move temp to final
                    if (rename(temp_file, output_file) == 0) {
                        success = true;
                    }
                } else {
                    LOG_error("Invalid M4A file: %s\n", temp_file);
                    unlink(temp_file);
                }
            } else {
                // Cleanup temp file
                unlink(temp_file);
                LOG_error("Download failed: %s\n", video_id);
            }
        }

        // Update queue item status
        pthread_mutex_lock(&queue_mutex);
        if (download_index < queue_count) {
            if (success) {
                download_status.completed_count++;
                // Remove successful download from queue
                for (int i = download_index; i < queue_count - 1; i++) {
                    download_queue[i] = download_queue[i + 1];
                }
                queue_count--;
            } else {
                download_queue[download_index].status = DOWNLOADER_STATUS_FAILED;
                download_queue[download_index].progress_percent = 0;
                download_status.failed_count++;
            }
        }
        pthread_mutex_unlock(&queue_mutex);

        // Reset speed/ETA for next item
        download_status.speed_bps = 0;
        download_status.eta_sec = 0;
    }

    // Re-enable auto sleep when downloads complete
    PWR_enableAutosleep();

    download_status.speed_bps = 0;
    download_status.eta_sec = 0;
    download_running = false;
    youtube_state = DOWNLOADER_STATE_IDLE;

    // Save queue state
    Downloader_saveQueue();

    return NULL;
}

int Downloader_downloadStart(void) {
    if (download_running) {
        return 0;  // Already running, thread will pick up new items
    }

    // Count pending items
    pthread_mutex_lock(&queue_mutex);
    int pending = 0;
    for (int i = 0; i < queue_count; i++) {
        if (download_queue[i].status == DOWNLOADER_STATUS_PENDING) {
            pending++;
        }
    }
    pthread_mutex_unlock(&queue_mutex);

    if (pending == 0) {
        return -1;  // Nothing to download
    }

    // Reset download status
    memset(&download_status, 0, sizeof(download_status));
    download_status.state = DOWNLOADER_STATE_DOWNLOADING;
    download_status.total_items = pending;

    download_running = true;
    download_should_stop = false;
    youtube_state = DOWNLOADER_STATE_DOWNLOADING;

    if (pthread_create(&download_thread, NULL, download_thread_func, NULL) != 0) {
        download_running = false;
        youtube_state = DOWNLOADER_STATE_ERROR;
        return -1;
    }

    pthread_detach(download_thread);
    return 0;
}

void Downloader_downloadStop(void) {
    if (download_running) {
        download_should_stop = true;
        // Thread is detached, just signal and return - no need to wait
    }
}

bool Downloader_isDownloading(void) {
    return download_running;
}

const DownloaderDownloadStatus* Downloader_getDownloadStatus(void) {
    download_status.state = youtube_state;
    return &download_status;
}

// Roughly what each transfer costs today, used only to weight the progress bar
// across whatever is actually missing. ffmpeg is the long pole by far.
#define YTDLP_EXPECTED_BYTES   40000000L
#define QJS_EXPECTED_BYTES      2600000L
#define FFMPEG_EXPECTED_BYTES  25600000L

// The percent range the running step owns, so a download can report its own
// 0-100 without knowing what else is queued behind it.
static int step_base_pct = 0;
static int step_span_pct = 0;

static void begin_step(const char* message, int base_pct, int span_pct) {
    step_base_pct = base_pct;
    step_span_pct = span_pct;
    update_status.progress_percent = base_pct;
    update_status.step_index++;
    strncpy(update_status.step_message, message, sizeof(update_status.step_message) - 1);
    update_status.step_message[sizeof(update_status.step_message) - 1] = '\0';
    update_status.status_detail[0] = '\0';
    update_status.download_bytes = 0;
    update_status.download_total = 0;
}

static void step_progress(long done, long total) {
    if (total <= 0) return;
    if (done > total) done = total;
    update_status.download_bytes = done;
    update_status.download_total = total;
    update_status.progress_percent = step_base_pct + (int)((long long)step_span_pct * done / total);
    snprintf(update_status.status_detail, sizeof(update_status.status_detail),
        "%.1fMB / %.1fMB", done / (1024.0 * 1024.0), total / (1024.0 * 1024.0));
}

// Stop the transfer writing to `dest`. Matching the destination rather than the
// user agent keeps this from killing an unrelated self-update download.
static void stop_fetch(const char* dest) {
    char cmd[900];
    snprintf(cmd, sizeof(cmd), "pkill -f 'wget.*%s' 2>/dev/null", dest);
    system(cmd);
}

// Ask the server how big the transfer is, following GitHub's redirects to the
// CDN that actually reports a length. Returns fallback when it cannot tell.
static long probe_download_size(const char* url, const char* temp_dir, long fallback) {
    char cmd[1600];
    char size_file[700];
    snprintf(size_file, sizeof(size_file), "%s/size.txt", temp_dir);

    // The last Content-Length is the CDN's, after GitHub's redirects
    char wget_bin[600];
    snprintf(wget_bin, sizeof(wget_bin), "%s/bin/wget", pak_path);
    snprintf(cmd, sizeof(cmd),
        "%s --spider -S --max-redirect=10 -T 30 -U \"%s\" \"%s\" 2>&1 "
        "| grep -i 'Content-Length' | tail -1 | awk '{print $2}' | tr -d '\\r' > \"%s\"",
        wget_bin, HTTP_USER_AGENT, url, size_file);
    system(cmd);

    long size = 0;
    FILE* f = fopen(size_file, "r");
    if (f) {
        if (fscanf(f, "%ld", &size) != 1) size = 0;
        fclose(f);
    }
    unlink(size_file);
    return size > 100000 ? size : fallback;
}

// Fetch `url` to `dest`, driving the current step's slice of the progress bar
// from the growing file. Returns false on cancel, transfer failure, or a file
// that came out too small to be the real thing.
static bool fetch_with_progress(const char* url, const char* dest,
                                long expected_bytes, long min_bytes,
                                const char* temp_dir) {
    char cmd[1800];
    char wget_bin[600];
    char done_marker[700];
    snprintf(wget_bin, sizeof(wget_bin), "%s/bin/wget", pak_path);
    snprintf(done_marker, sizeof(done_marker), "%s/fetch_done.txt", temp_dir);
    unlink(done_marker);

    long total = probe_download_size(url, temp_dir, expected_bytes);
    step_progress(0, total);

    if (update_should_stop) return false;

    // wget runs detached so the file size can be sampled while it works, and
    // records its exit status: a file of plausible size proves nothing on its own
    snprintf(cmd, sizeof(cmd),
        "(%s -T 120 -t 3 -q -U \"%s\" -O \"%s\" \"%s\"; echo $? > \"%s\") &",
        wget_bin, HTTP_USER_AGENT, dest, url, done_marker);
    system(cmd);

    const int poll_interval_us = 500000;
    const int max_polls = 600 * (1000000 / poll_interval_us);  // 10 minutes
    int elapsed_polls = 0;

    while (elapsed_polls < max_polls) {
        if (update_should_stop) {
            stop_fetch(dest);
            unlink(dest);
            return false;
        }
        if (access(done_marker, F_OK) == 0) break;

        struct stat st;
        if (stat(dest, &st) == 0 && st.st_size > 0) {
            step_progress(st.st_size, total);
        }

        usleep(poll_interval_us);
        elapsed_polls++;
    }

    // Timing out only ends the wait; without this the transfer keeps running
    // and writing to a file we are about to remove
    if (elapsed_polls >= max_polls) {
        stop_fetch(dest);
    }

    int fetch_exit = -1;
    FILE* marker = fopen(done_marker, "r");
    if (marker) {
        if (fscanf(marker, "%d", &fetch_exit) != 1) fetch_exit = -1;
        fclose(marker);
    }
    unlink(done_marker);

    struct stat st;
    if (fetch_exit != 0 || stat(dest, &st) != 0 || st.st_size < min_bytes) {
        unlink(dest);
        return false;
    }

    step_progress(st.st_size, st.st_size);
    return true;
}

// yt-dlp tracks the nightly channel: YouTube changes how it serves audio every
// few weeks, and a fix reaches nightly the same day but a stable release only
// when one is cut. Its version is compared against state/yt-dlp_version.txt.
#define YTDLP_RELEASE_API_URL "https://api.github.com/repos/yt-dlp/yt-dlp-nightly-builds/releases/latest"
#define YTDLP_ASSET_NAME "yt-dlp_linux_aarch64"

// The other two are single static aarch64 binaries published as release assets,
// so a plain fetch is enough - no version tracking, since any build yt-dlp
// accepts will do and neither is on YouTube's moving side. ffmpeg is taken
// gzipped: half the bytes of the 51 MB binary, and the firmware has gunzip.
#define QJS_DOWNLOAD_URL "https://github.com/quickjs-ng/quickjs/releases/latest/download/qjs-linux-aarch64"
#define FFMPEG_DOWNLOAD_URL "https://github.com/eugeneware/ffmpeg-static/releases/latest/download/ffmpeg-linux-arm64.gz"

// Move `src` to `dst` durably, across filesystems: staging is on tmpfs and the
// pak is on the SD card, so rename(2) is unusable here - it fails with EXDEV.
// Returns true once `dst` holds the whole file and it has reached the card, at
// which point `src` has been removed. On failure `dst` is cleaned up and `src`
// is left untouched, so the caller still has the download to retry from.
static bool safe_rename(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return false;

    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out < 0) {
        close(in);
        return false;
    }

    char buf[64 * 1024];
    bool ok = true;
    ssize_t got;
    while ((got = read(in, buf, sizeof(buf))) > 0) {
        ssize_t done = 0;
        while (done < got) {
            ssize_t put = write(out, buf + done, (size_t)(got - done));
            if (put < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            done += put;
        }
        if (!ok) break;
    }
    if (got < 0) ok = false;

    // Without this the rename below can publish a name whose contents are still
    // only in page cache, so losing power leaves a truncated binary in place
    if (ok && fsync(out) != 0) ok = false;
    if (close(out) != 0) ok = false;
    close(in);

    if (!ok) {
        unlink(dst);
        return false;
    }
    unlink(src);
    return true;
}

// Put a staged file into place without ever leaving a partial copy at dest.
// It lands beside the target as .new, and only a same-directory rename - atomic
// - swaps it in.
static bool install_staged_binary(const char* staged, const char* dest, long min_bytes) {
    char pending[700];
    snprintf(pending, sizeof(pending), "%s.new", dest);
    unlink(pending);

    if (!safe_rename(staged, pending)) {
        unlink(pending);
        return false;
    }

    struct stat st;
    if (stat(pending, &st) != 0 || st.st_size < min_bytes) {
        unlink(pending);
        return false;
    }

    chmod(pending, 0755);
    if (rename(pending, dest) != 0) {
        unlink(pending);
        return false;
    }
    return true;
}

// Fetch the interpreter that solves YouTube's JS challenges.
static bool install_quickjs(const char* temp_dir, int base_pct, int span_pct) {
    begin_step("Downloading qjs", base_pct, span_pct);

    char staged[700];
    snprintf(staged, sizeof(staged), "%s/qjs.new", temp_dir);
    if (!fetch_with_progress(QJS_DOWNLOAD_URL, staged, QJS_EXPECTED_BYTES, 100000, temp_dir)) {
        return false;
    }

    return install_staged_binary(staged, qjs_path, 100000);
}

// Fetch ffmpeg for images whose firmware has none, expanding it in place.
static bool install_ffmpeg(const char* temp_dir, int base_pct, int span_pct) {
    begin_step("Downloading ffmpeg", base_pct, span_pct);

    char staged_gz[700];
    char staged[700];
    snprintf(staged_gz, sizeof(staged_gz), "%s/ffmpeg.gz", temp_dir);
    snprintf(staged, sizeof(staged), "%s/ffmpeg.new", temp_dir);
    if (!fetch_with_progress(FFMPEG_DOWNLOAD_URL, staged_gz, FFMPEG_EXPECTED_BYTES, 10000000, temp_dir)) {
        return false;
    }

    strncpy(update_status.step_message, "Extracting ffmpeg",
            sizeof(update_status.step_message) - 1);
    update_status.status_detail[0] = '\0';

    char cmd[1600];
    snprintf(cmd, sizeof(cmd), "gunzip -c \"%s\" > \"%s\"", staged_gz, staged);
    int rc = system(cmd);
    unlink(staged_gz);

    struct stat st;
    if (rc != 0 || stat(staged, &st) != 0 || st.st_size < 20000000) {
        unlink(staged);
        return false;
    }

    return install_staged_binary(staged, ffmpeg_path, 20000000);
}

static void* update_thread_func(void* arg) {
    (void)arg;


    update_status.updating = true;
    update_status.progress_percent = 0;

    // Check connectivity
    int conn = system("ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1");
    if (conn != 0) {
        conn = system("ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1");
    }

    if (conn != 0) {
        strncpy(update_status.error_message, "No internet connection", sizeof(update_status.error_message) - 1);
        update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
        update_status.updating = false;
        update_running = false;
        return NULL;
    }

    // Check for cancellation
    if (update_should_stop) {
        update_status.updating = false;
        update_running = false;
        return NULL;
    }

    update_status.progress_percent = 10;

    // Fetch latest version from GitHub API
    char temp_dir[512];
    snprintf(temp_dir, sizeof(temp_dir), "/tmp/ytdlp_update_%d", getpid());
    mkdir(temp_dir, 0755);

    char latest_file[600];
    snprintf(latest_file, sizeof(latest_file), "%s/latest.json", temp_dir);

    char cmd[1024];
    char error_file[600];
    char wget_bin[600];
    snprintf(error_file, sizeof(error_file), "%s/wget_error.txt", temp_dir);
    snprintf(wget_bin, sizeof(wget_bin), "%s/bin/wget", pak_path);

    update_status.progress_percent = 15;

    // Use timeout to prevent indefinite blocking on slow/unstable WiFi
    snprintf(cmd, sizeof(cmd),
        "%s -q -T 30 -t 2 -U \"%s\" -O \"%s\" "
        "\"" YTDLP_RELEASE_API_URL "\" 2>\"%s\"",
        wget_bin, HTTP_USER_AGENT, latest_file, error_file);

    int fetch_result = system(cmd);
    if (fetch_result != 0 || access(latest_file, F_OK) != 0) {
        // Copy error file to pak for debugging
        char debug_cmd[1024];
        snprintf(debug_cmd, sizeof(debug_cmd), "cp \"%s\" \"%s/state/wget_error.txt\" 2>/dev/null", error_file, pak_path);
        system(debug_cmd);

        // Try to read the actual error
        FILE* ef = fopen(error_file, "r");
        if (ef) {
            char err_line[128];
            if (fgets(err_line, sizeof(err_line), ef)) {
                char* nl = strchr(err_line, '\n');
                if (nl) *nl = '\0';
                // Shorter prefix to avoid truncation
                strncpy(update_status.error_message, err_line, sizeof(update_status.error_message) - 1);
            } else {
                snprintf(update_status.error_message, sizeof(update_status.error_message),
                    "wget error %d", WEXITSTATUS(fetch_result));
            }
            fclose(ef);
        } else {
            strncpy(update_status.error_message, "Failed to check GitHub", sizeof(update_status.error_message) - 1);
            update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
        }
        update_status.updating = false;
        update_running = false;
        return NULL;
    }

    // Check for cancellation after the fetch
    if (update_should_stop) {
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
        system(cmd);
        update_status.updating = false;
        update_running = false;
        return NULL;
    }

    update_status.progress_percent = 30;

    // Parse version from JSON (simple grep approach)
    char version_cmd[1024];
    snprintf(version_cmd, sizeof(version_cmd),
        "grep -o '\"tag_name\": *\"[^\"]*' \"%s\" | cut -d'\"' -f4",
        latest_file);

    char latest_version[32] = "";
    FILE* pipe = popen(version_cmd, "r");
    if (pipe) {
        if (fgets(latest_version, sizeof(latest_version), pipe)) {
            char* nl = strchr(latest_version, '\n');
            if (nl) *nl = '\0';
        }
        pclose(pipe);
    }

    if (strlen(latest_version) == 0) {
        strncpy(update_status.error_message, "Could not parse version", sizeof(update_status.error_message) - 1);
        update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
        update_status.updating = false;
        update_running = false;
        return NULL;
    }

    strncpy(update_status.latest_version, latest_version, sizeof(update_status.latest_version));
    strncpy(update_status.current_version, current_version, sizeof(update_status.current_version));

    // Work out what this run actually has to fetch. Any of the three can be
    // missing on its own: an interrupted install, a pak that predates them, or
    // simply a current yt-dlp with no interpreter beside it.
    bool need_ytdlp  = (strcmp(latest_version, current_version) != 0);
    bool need_qjs    = !qjs_present();
    bool need_ffmpeg = !ffmpeg_present();

    update_status.update_available = need_ytdlp;
    update_status.step_index = 0;
    update_status.step_count = (need_ytdlp ? 1 : 0) + (need_qjs ? 1 : 0) + (need_ffmpeg ? 1 : 0);

    if (update_status.step_count == 0) {
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
        system(cmd);
        update_status.progress_percent = 100;
        update_status.updating = false;
        update_running = false;
        return NULL;
    }

    // Share the bar out by transfer size, so the 25 MB ffmpeg does not race
    // past in the same width as the 2 MB interpreter
    const int span_start = 20;
    const int span_all = 78;
    long weight_total = (need_ytdlp ? YTDLP_EXPECTED_BYTES : 0) +
                        (need_qjs ? QJS_EXPECTED_BYTES : 0) +
                        (need_ffmpeg ? FFMPEG_EXPECTED_BYTES : 0);
    long weight_done = 0;

    if (update_should_stop) {
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
        system(cmd);
        update_status.updating = false;
        update_running = false;
        return NULL;
    }

    if (need_ytdlp) {
        int base = span_start + (int)((long long)span_all * weight_done / weight_total);
        int span = (int)((long long)span_all * YTDLP_EXPECTED_BYTES / weight_total);

        char url_cmd[1024];
        snprintf(url_cmd, sizeof(url_cmd),
            "grep -o '\"browser_download_url\": *\"[^\"]*" YTDLP_ASSET_NAME "\"' \"%s\" | cut -d'\"' -f4",
            latest_file);

        char download_url[512] = "";
        pipe = popen(url_cmd, "r");
        if (pipe) {
            if (fgets(download_url, sizeof(download_url), pipe)) {
                char* nl = strchr(download_url, '\n');
                if (nl) *nl = '\0';
            }
            pclose(pipe);
        }

        if (strlen(download_url) == 0) {
            strncpy(update_status.error_message, "No ARM64 binary found", sizeof(update_status.error_message) - 1);
            update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
            snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
            system(cmd);
            update_status.updating = false;
            update_running = false;
            return NULL;
        }

        begin_step("Downloading yt-dlp", base, span);

        char new_binary[600];
        snprintf(new_binary, sizeof(new_binary), "%s/yt-dlp.new", temp_dir);
        if (!fetch_with_progress(download_url, new_binary, YTDLP_EXPECTED_BYTES, 1000000, temp_dir)) {
            if (!update_should_stop) {
                strncpy(update_status.error_message, "Download failed", sizeof(update_status.error_message) - 1);
                update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
            }
            snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
            system(cmd);
            update_status.updating = false;
            update_running = false;
            return NULL;
        }

        strncpy(update_status.step_message, "Installing yt-dlp", sizeof(update_status.step_message) - 1);

        // The working binary stays untouched until the swap succeeds
        if (!install_staged_binary(new_binary, ytdlp_path, 1000000)) {
            strncpy(update_status.error_message, "Failed to install update", sizeof(update_status.error_message) - 1);
            update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
            snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
            system(cmd);
            update_status.updating = false;
            update_running = false;
            return NULL;
        }

        // Only now does the recorded version describe what is on disk
        FILE* vf = fopen(version_file, "w");
        if (vf) {
            fprintf(vf, "%s\n", latest_version);
            fclose(vf);
        }
        strncpy(current_version, latest_version, sizeof(current_version) - 1);
        current_version[sizeof(current_version) - 1] = '\0';

        weight_done += YTDLP_EXPECTED_BYTES;
    }

    if (need_qjs && !update_should_stop) {
        int base = span_start + (int)((long long)span_all * weight_done / weight_total);
        int span = (int)((long long)span_all * QJS_EXPECTED_BYTES / weight_total);
        if (!install_quickjs(temp_dir, base, span)) {
            strncpy(update_status.error_message, "Could not install script engine",
                    sizeof(update_status.error_message) - 1);
            update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
        }
        weight_done += QJS_EXPECTED_BYTES;
    }

    if (need_ffmpeg && !update_should_stop && update_status.error_message[0] == '\0') {
        int base = span_start + (int)((long long)span_all * weight_done / weight_total);
        int span = (int)((long long)span_all * FFMPEG_EXPECTED_BYTES / weight_total);
        if (!install_ffmpeg(temp_dir, base, span)) {
            strncpy(update_status.error_message, "Could not install media tools",
                    sizeof(update_status.error_message) - 1);
            update_status.error_message[sizeof(update_status.error_message) - 1] = '\0';
        }
    }

    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", temp_dir);
    system(cmd);

    update_status.step_message[0] = '\0';
    update_status.progress_percent = 100;
    update_status.updating = false;
    update_running = false;

    return NULL;
}

int Downloader_checkForUpdate(void) {
    if (update_running) return 0;

    // Just check version without downloading
    memset(&update_status, 0, sizeof(update_status));
    strncpy(update_status.current_version, current_version, sizeof(update_status.current_version));

    return 0;
}

int Downloader_startUpdate(void) {
    if (update_running) return 0;

    memset(&update_status, 0, sizeof(update_status));
    strncpy(update_status.current_version, current_version, sizeof(update_status.current_version));

    update_running = true;
    update_should_stop = false;
    update_status.updating = true;
    youtube_state = DOWNLOADER_STATE_UPDATING;

    if (pthread_create(&update_thread, NULL, update_thread_func, NULL) != 0) {
        update_running = false;
        update_status.updating = false;
        youtube_state = DOWNLOADER_STATE_ERROR;
        strncpy(error_message, "Failed to create update thread", sizeof(error_message) - 1);
        error_message[sizeof(error_message) - 1] = '\0';
        return -1;
    }

    pthread_detach(update_thread);
    return 0;
}

void Downloader_cancelUpdate(void) {
    if (update_running) {
        update_should_stop = true;
        // Thread is detached, just signal and return - no need to wait
    }
}

const DownloaderUpdateStatus* Downloader_getUpdateStatus(void) {
    return &update_status;
}

DownloaderState Downloader_getState(void) {
    return youtube_state;
}

const char* Downloader_getError(void) {
    return error_message;
}

void Downloader_update(void) {
    // Check if threads finished
    if (!download_running && youtube_state == DOWNLOADER_STATE_DOWNLOADING) {
        youtube_state = DOWNLOADER_STATE_IDLE;
    }
    if (!update_running && youtube_state == DOWNLOADER_STATE_UPDATING) {
        youtube_state = DOWNLOADER_STATE_IDLE;
    }
}

void Downloader_saveQueue(void) {
    pthread_mutex_lock(&queue_mutex);

    FILE* f = fopen(queue_file, "w");
    if (f) {
        for (int i = 0; i < queue_count; i++) {
            // Only save pending items
            if (download_queue[i].status == DOWNLOADER_STATUS_PENDING) {
                fprintf(f, "%s|%s\n",
                    download_queue[i].video_id,
                    download_queue[i].title);
            }
        }
        fclose(f);
    }

    pthread_mutex_unlock(&queue_mutex);
}

void Downloader_loadQueue(void) {
    pthread_mutex_lock(&queue_mutex);

    queue_count = 0;

    FILE* f = fopen(queue_file, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f) && queue_count < DOWNLOADER_MAX_QUEUE) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';

            char* video_id = strtok(line, "|");
            char* title = strtok(NULL, "|");

            if (video_id && title) {
                strncpy(download_queue[queue_count].video_id, video_id, DOWNLOADER_VIDEO_ID_LEN - 1);
                strncpy(download_queue[queue_count].title, title, DOWNLOADER_MAX_TITLE - 1);
                download_queue[queue_count].status = DOWNLOADER_STATUS_PENDING;
                download_queue[queue_count].progress_percent = 0;
                queue_count++;
            }
        }
        fclose(f);
    }

    pthread_mutex_unlock(&queue_mutex);

}

const char* Downloader_getDownloadPath(void) {
    return download_dir;
}

static void sanitize_filename(const char* input, char* output, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; input[i] && j < max_len - 1; i++) {
        unsigned char c = (unsigned char)input[i];

        // Allow UTF-8 multi-byte sequences (bytes >= 0x80)
        // This preserves Korean, Japanese, Chinese, and other Unicode characters
        if (c >= 0x80) {
            output[j++] = (char)c;
            continue;
        }

        // Allow ASCII alphanumeric and safe symbols
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == ' ' || c == '.' || c == '_' || c == '-' ||
            c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '!' || c == ',' || c == '\'') {
            output[j++] = (char)c;
        }
        // Skip filesystem-unsafe characters: / \ : * ? " < > |
    }
    output[j] = '\0';

    // Trim to 120 bytes (allow longer names for CJK which use 3 bytes per char)
    if (j > 120) {
        // Find a safe truncation point (don't cut in middle of UTF-8 sequence)
        j = 120;
        while (j > 0 && (output[j] & 0xC0) == 0x80) {
            j--;  // Back up to start of UTF-8 sequence
        }
        output[j] = '\0';
    }

    // Trim trailing/leading spaces
    while (j > 0 && output[j-1] == ' ') {
        output[--j] = '\0';
    }

    char* start = output;
    while (*start == ' ') start++;
    if (start != output) {
        memmove(output, start, strlen(start) + 1);
    }

    // Default if empty
    if (output[0] == '\0') {
        strncpy(output, "download", max_len - 1);
        output[max_len - 1] = '\0';
    }
}

static int run_command(const char* cmd, char* output, size_t output_size) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return -1;

    if (output && output_size > 0) {
        output[0] = '\0';
        size_t total = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe) && total < output_size - 1) {
            size_t len = strlen(buf);
            if (total + len < output_size) {
                strcat(output, buf);
                total += len;
            }
        }
    }

    return pclose(pipe);
}
