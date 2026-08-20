#define _GNU_SOURCE
#include "wget_fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "defines.h"
#include "api.h"

// curl is part of the firmware on every supported device, so no HTTP client is
// bundled - only the CA bundle it has nowhere else to get. -L follows redirects;
// a speed floor catches stalls without cutting off a slow but healthy transfer,
// which a total time limit would. Keep to flags curl 7.54 knows: that is what
// the devices carry, and it rejects anything newer.
#define FETCH_BIN "curl -sSL"
#define FETCH_STALL_GUARD "--speed-time 30 --speed-limit 512"

// Pak root is the working directory
#define CA_BUNDLE_PATH "./res/cacert.pem"

static char tls_flags[256] = "";

void http_tls_init(void) {
    if (tls_flags[0] != '\0') return;

    // Always verify. A missing bundle is a broken install, and every transfer
    // failing loudly beats silently downloading an executable nobody checked.
    if (access(CA_BUNDLE_PATH, R_OK) != 0) {
        LOG_error("[Fetch] %s unreadable, HTTPS transfers will fail\n", CA_BUNDLE_PATH);
    }
    snprintf(tls_flags, sizeof(tls_flags), "--cacert '%s'", CA_BUNDLE_PATH);
}

// Resolved at startup on the main thread, so worker threads only ever read it
const char* http_tls_flags(void) {
    if (tls_flags[0] == '\0') http_tls_init();
    return tls_flags;
}

// Escape a string for use inside single quotes in shell commands.
// Caller must provide a buffer at least 4x the length of src.
static void shell_escape_single(const char* src, char* dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 5; i++) {
        if (src[i] == '\'') {
            // End quote, escaped apostrophe, reopen quote: '\''
            dst[j++] = '\'';
            dst[j++] = '\\';
            dst[j++] = '\'';
            dst[j++] = '\'';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

int wget_fetch(const char* url, uint8_t* buffer, int buffer_size) {
    if (!url || !buffer || buffer_size <= 0) {
        LOG_error("[WgetFetch] Invalid parameters\n");
        return -1;
    }

    // Use temp file approach (reliable from within app process, same as selfupdate.c)
    // popen + "-O -" has pipe issues when called from SDL/audio threads
    char tmpfile[128];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/wget_%d.tmp", getpid());

    char safe_url[4096];
    shell_escape_single(url, safe_url, sizeof(safe_url));

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        FETCH_BIN " --fail %s --connect-timeout 15 --max-time 30 --retry 2"
        " -o '%s' '%s' 2>/dev/null",
        http_tls_flags(), tmpfile, safe_url);

    int ret = system(cmd);

    if (ret != 0) {
        LOG_error("[WgetFetch] Failed to fetch: %s (exit=%d)\n", url, ret);
        unlink(tmpfile);
        return -1;
    }

    // Read temp file into buffer
    FILE* fp = fopen(tmpfile, "rb");
    if (!fp) {
        LOG_error("[WgetFetch] Failed to open temp file for: %s\n", url);
        unlink(tmpfile);
        return -1;
    }

    int total = fread(buffer, 1, buffer_size - 1, fp);
    fclose(fp);
    unlink(tmpfile);

    if (total <= 0) {
        LOG_error("[WgetFetch] Empty response for: %s\n", url);
        return -1;
    }

    return total;
}

int wget_download_file(const char* url, const char* filepath,
                       volatile int* progress_pct, volatile bool* should_stop,
                       volatile int* speed_bps_out, volatile int* eta_sec_out) {
    if (!url || !filepath) {
        LOG_error("[WgetFetch] download: invalid parameters\n");
        return -1;
    }

    if (progress_pct) *progress_pct = 0;
    if (speed_bps_out) *speed_bps_out = 0;
    if (eta_sec_out) *eta_sec_out = 0;

    // Shell-escape URL and filepath for safe use in shell commands
    char safe_url[4096];
    char safe_filepath[2048];
    shell_escape_single(url, safe_url, sizeof(safe_url));
    shell_escape_single(filepath, safe_filepath, sizeof(safe_filepath));

    // Step 1: Start the download in background with a completion marker
    char cmd[8192];
    char done_marker[512];
    snprintf(done_marker, sizeof(done_marker), "%s.done", filepath);
    char headers_file[512];
    snprintf(headers_file, sizeof(headers_file), "%s.headers", filepath);

    char pid_file[512];
    snprintf(pid_file, sizeof(pid_file), "%s.pid", filepath);
    char safe_done_marker[2048];
    char safe_pid_file[2048];
    char safe_headers_file[2048];
    shell_escape_single(done_marker, safe_done_marker, sizeof(safe_done_marker));
    shell_escape_single(pid_file, safe_pid_file, sizeof(safe_pid_file));
    shell_escape_single(headers_file, safe_headers_file, sizeof(safe_headers_file));

    // Remove any stale markers
    unlink(done_marker);
    unlink(headers_file);
    unlink(pid_file);

    // Download (proven working: -q with stderr to /dev/null)
    snprintf(cmd, sizeof(cmd),
        "(" FETCH_BIN " --fail %s --connect-timeout 30 " FETCH_STALL_GUARD " --retry 2"
        " -o '%s' '%s' 2>/dev/null & echo $! > '%s'; wait $!;"
        " echo $? > '%s.part'; mv '%s.part' '%s') &",
        http_tls_flags(), safe_filepath, safe_url, safe_pid_file,
        safe_done_marker, safe_done_marker, safe_done_marker);
    system(cmd);

    // Best-effort: probe content-length via spider in background
    snprintf(cmd, sizeof(cmd),
        FETCH_BIN " %s -I --connect-timeout 10"
        " '%s' >'%s' 2>/dev/null &",
        http_tls_flags(), safe_url, safe_headers_file);
    system(cmd);

    // Step 2: Poll file size for progress with speed/stall tracking
    // Parse Content-Length from headers file during polling
    int result = -1;
    long total_size = 0;
    bool headers_parsed = false;
    long prev_size = 0;
    struct timespec prev_time;
    clock_gettime(CLOCK_MONOTONIC, &prev_time);
    struct timespec stall_start = prev_time;
    long stall_size = 0;  // File size when stall tracking started

    // Check initial file size
    struct stat init_st;
    if (stat(filepath, &init_st) == 0) {
        prev_size = init_st.st_size;
        stall_size = init_st.st_size;
    }

    while (!(should_stop && *should_stop)) {
        // Check if download is complete
        if (access(done_marker, F_OK) == 0) {
            break;
        }

        // Parse Content-Length from headers file (take the last one, after redirects)
        if (!headers_parsed) {
            FILE* hf = fopen(headers_file, "r");
            if (hf) {
                char line[256];
                long last_cl = 0;
                while (fgets(line, sizeof(line), hf)) {
                    const char* p = line;
                    while (*p == ' ' || *p == '\t') p++;
                    if (strncasecmp(p, "Content-Length:", 15) == 0) {
                        long val = atol(p + 15);
                        if (val > 0) last_cl = val;
                    }
                }
                fclose(hf);
                // Only trust if it looks like a real file size (> 1KB)
                if (last_cl > 1024) {
                    total_size = last_cl;
                    headers_parsed = true;
                }
            }
        }

        struct stat st;
        long curr_size = 0;
        if (stat(filepath, &st) == 0) {
            curr_size = st.st_size;

            // Update progress
            if (progress_pct && total_size > 0) {
                int pct = (int)((curr_size * 100) / total_size);
                if (pct > 99) pct = 99;
                *progress_pct = pct;
            }
        }

        // Speed and ETA calculation (every poll cycle)
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - prev_time.tv_sec) +
                         (now.tv_nsec - prev_time.tv_nsec) / 1e9;

        if (elapsed >= 1.0) {
            long bytes_delta = curr_size - prev_size;
            int speed = (int)(bytes_delta / elapsed);
            if (speed < 0) speed = 0;
            if (speed_bps_out) *speed_bps_out = speed;
            if (eta_sec_out && speed > 0 && total_size > 0) {
                *eta_sec_out = (int)((total_size - curr_size) / speed);
            } else if (eta_sec_out) {
                *eta_sec_out = 0;
            }
            prev_size = curr_size;
            prev_time = now;
        }

        // Stall detection: if file size hasn't changed for 60 seconds, kill it
        if (curr_size != stall_size) {
            stall_size = curr_size;
            stall_start = now;
        } else {
            double stall_elapsed = (now.tv_sec - stall_start.tv_sec) +
                                   (now.tv_nsec - stall_start.tv_nsec) / 1e9;
            if (stall_elapsed >= 60.0) {
                LOG_error("[WgetFetch] download stalled for 60s, killing: %s\n", url);
                snprintf(cmd, sizeof(cmd), "kill $(cat '%s') 2>/dev/null", safe_pid_file);
                system(cmd);
                unlink(done_marker);
                unlink(headers_file);
                unlink(pid_file);
                // Keep partial file for resume
                if (speed_bps_out) *speed_bps_out = 0;
                if (eta_sec_out) *eta_sec_out = 0;
                return -1;
            }
        }

        usleep(200000);  // 200ms polling interval
    }

    // Step 4: Handle cancellation
    if (should_stop && *should_stop) {
        // Kill the transfer and clean up — remove file on cancel
        snprintf(cmd, sizeof(cmd), "kill $(cat '%s') 2>/dev/null", safe_pid_file);
        system(cmd);
        unlink(done_marker);
        unlink(headers_file);
        unlink(pid_file);
        unlink(filepath);
        if (speed_bps_out) *speed_bps_out = 0;
        if (eta_sec_out) *eta_sec_out = 0;
        return -1;
    }

    // Step 5: Verify download. The marker holds curl's exit status: a non-empty
    // file proves nothing on its own, since a truncated transfer leaves one too.
    int fetch_exit = -1;
    FILE* marker = fopen(done_marker, "r");
    if (marker) {
        if (fscanf(marker, "%d", &fetch_exit) != 1) fetch_exit = -1;
        fclose(marker);
    }

    unlink(done_marker);
    unlink(headers_file);
    unlink(pid_file);
    if (speed_bps_out) *speed_bps_out = 0;
    if (eta_sec_out) *eta_sec_out = 0;

    struct stat st;
    if (fetch_exit == 0 && stat(filepath, &st) == 0 && st.st_size > 0) {
        result = (int)st.st_size;
        if (progress_pct) *progress_pct = 100;
    } else {
        LOG_error("[WgetFetch] download failed: %s\n", url);
        // Don't unlink — keep partial file for resume on retry
    }

    return result;
}
