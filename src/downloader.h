#ifndef __DOWNLOADER_H__
#define __DOWNLOADER_H__

#include <stdint.h>
#include <stdbool.h>

#define DOWNLOADER_MAX_RESULTS 30
#define DOWNLOADER_MAX_QUEUE 100
#define DOWNLOADER_MAX_TITLE 256
#define DOWNLOADER_MAX_ARTIST 128
#define DOWNLOADER_VIDEO_ID_LEN 16

// Search result
typedef struct {
    char video_id[DOWNLOADER_VIDEO_ID_LEN];
    char title[DOWNLOADER_MAX_TITLE];
    char artist[DOWNLOADER_MAX_ARTIST];
    int duration_sec;
} DownloaderResult;

// Queue item status
typedef enum {
    DOWNLOADER_STATUS_PENDING = 0,
    DOWNLOADER_STATUS_DOWNLOADING,
    DOWNLOADER_STATUS_COMPLETE,
    DOWNLOADER_STATUS_FAILED
} DownloaderItemStatus;

// Download queue item
typedef struct {
    char video_id[DOWNLOADER_VIDEO_ID_LEN];
    char title[DOWNLOADER_MAX_TITLE];
    DownloaderItemStatus status;
    int progress_percent;  // 0-100 during download
    int speed_bps;         // Download speed in bytes/sec
    int eta_sec;           // Estimated time remaining in seconds
} DownloaderQueueItem;

// Module states
typedef enum {
    DOWNLOADER_STATE_IDLE = 0,
    DOWNLOADER_STATE_SEARCHING,
    DOWNLOADER_STATE_DOWNLOADING,
    DOWNLOADER_STATE_UPDATING,
    DOWNLOADER_STATE_ERROR
} DownloaderState;

// Download status info
typedef struct {
    DownloaderState state;
    int current_index;           // Currently downloading item index
    int total_items;             // Total items in queue
    int completed_count;         // Number completed
    int failed_count;            // Number failed
    char current_title[DOWNLOADER_MAX_TITLE];
    char error_message[256];
    int speed_bps;               // Current download speed
    int eta_sec;                 // Current ETA
} DownloaderDownloadStatus;

// What the helpers screen is showing, and therefore what A does there. Derived
// from one snapshot so the text and the button hint cannot disagree.
typedef enum {
    YTDLP_UI_UNCHECKED,   // no check has run yet
    YTDLP_UI_CHECKING,
    YTDLP_UI_AVAILABLE,   // something to install or update
    YTDLP_UI_CURRENT,     // everything present and current
    YTDLP_UI_INSTALLING,
    YTDLP_UI_INSTALLED,
    YTDLP_UI_FAILED
} YtdlpUiState;

// Update status info
typedef struct {
    bool update_available;      // the check found something to fetch
    bool fresh_install;         // a helper is absent, so this installs rather than updates
    bool checking;              // a version check is in flight
    bool check_complete;        // a check has come back
    long estimated_bytes;       // what confirming would download
    char plan_summary[64];      // what it would install, e.g. "yt-dlp, ffmpeg"
    char current_version[32];
    char latest_version[32];
    bool updating;
    int progress_percent;
    long download_bytes;        // Bytes downloaded so far
    long download_total;        // Total bytes to download (0 if unknown)
    char status_detail[64];     // Detailed status (e.g., "2.5 MB / 5.0 MB")
    char step_message[64];      // Current step (e.g., "Downloading media tools")
    int step_index;             // 1-based position of the current step
    int step_count;             // Total steps this run
    char error_message[256];
} DownloaderUpdateStatus;

// Search status info
typedef struct {
    bool searching;             // True while search is in progress
    bool completed;             // True when search finished (success or error)
    int result_count;           // Number of results found (-1 on error)
    char error_message[256];    // Error message if failed
} DownloaderSearchStatus;

// Reported version when the yt-dlp binary is not installed
#define DOWNLOADER_VERSION_NOT_INSTALLED "Not installed"

// Initialize downloader module
// Safe to call repeatedly: paths and the queue are set up on the first call,
// so a later install can finish initialization without an app restart.
// Returns 0 on success, -1 if yt-dlp is not installed
int Downloader_init(void);

// Re-read the yt-dlp version, probing the binary when the cached value is
// missing or stale. Reports DOWNLOADER_VERSION_NOT_INSTALLED when absent.
void Downloader_refreshVersion(void);

// Cleanup resources
void Downloader_cleanup(void);

// Check if yt-dlp binary exists
bool Downloader_isAvailable(void);

// Check network connectivity (quick ping test)
// Returns true if network is available, false otherwise
bool Downloader_checkNetwork(void);

// Get yt-dlp version
const char* Downloader_getVersion(void);

// Async search functions
// Start a background search
int Downloader_startSearch(const char* query);

// Get search status (call in main loop to check progress)
const DownloaderSearchStatus* Downloader_getSearchStatus(void);

// Get search results after search completes
// Returns pointer to internal results array, count is set via status->result_count
DownloaderResult* Downloader_getSearchResults(void);

// Cancel ongoing search
void Downloader_cancelSearch(void);

// Queue management
int Downloader_queueAdd(const char* video_id, const char* title);
int Downloader_queueRemove(int index);
int Downloader_queueRemoveById(const char* video_id);
int Downloader_queueClear(void);
int Downloader_queueCount(void);
DownloaderQueueItem* Downloader_queueGet(int* count);

// Check if video is already in queue or downloaded
bool Downloader_isInQueue(const char* video_id);
bool Downloader_isDownloaded(const char* video_id);

// Start downloading queue items (runs in background)
int Downloader_downloadStart(void);

// Stop/cancel current download
void Downloader_downloadStop(void);

// Get download status
const DownloaderDownloadStatus* Downloader_getDownloadStatus(void);

// Check if download thread is running
bool Downloader_isDownloading(void);

// yt-dlp update functions
int Downloader_checkForUpdate(void);  // Check if new version available

// Find out what would be installed, without installing it. Runs in the
// background; watch DownloaderUpdateStatus.checking for the answer.
int Downloader_startUpdateCheck(void);

// Install what the last check found. Only meaningful after a check came back
// reporting update_available.
int Downloader_startUpdate(void);
void Downloader_cancelUpdate(void);   // Cancel update
const DownloaderUpdateStatus* Downloader_getUpdateStatus(void);

// A by-value copy of the update status. The check and the install run on worker
// threads and can flip fields part-way through a frame, so anything reading more
// than one field must take a copy once and use that.
DownloaderUpdateStatus Downloader_getUpdateSnapshot(void);

// Boil the update status down to what a screen needs to show and offer.
YtdlpUiState Downloader_updateUiState(const DownloaderUpdateStatus* status);

// Get current state
DownloaderState Downloader_getState(void);

// Get last error message
const char* Downloader_getError(void);

// Update function (call in main loop)
void Downloader_update(void);

// Save/load queue (persistence)
void Downloader_saveQueue(void);
void Downloader_loadQueue(void);

// Get download directory path
const char* Downloader_getDownloadPath(void);

#endif
