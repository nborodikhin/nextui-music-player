#define _GNU_SOURCE
#include "wget_fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "file_utils.h"
#include "defines.h"
#include "api.h"

// curl is part of the firmware on every supported device, so no HTTP client is
// bundled - only the CA bundle it has nowhere else to get. -L follows redirects,
// -S keeps error messages coming after -s silences the progress meter. A speed
// floor catches stalls without cutting off a slow but healthy transfer, which a
// total time limit would. Keep to flags curl 7.54 knows: that is what the
// devices carry, and it rejects anything newer.

// Pak root is the working directory
#define CA_BUNDLE_PATH "./res/cacert.pem"

// Identifies us to the servers we talk to. The GitHub API requires a
// User-Agent, and a name beats curl/x.y in a podcast host's logs.
#define HTTP_USER_AGENT "NextUI-Music-Player"

const char* http_tls_flags(void) {
    return "--cacert \"" CA_BUNDLE_PATH "\"";
}

// Clean up after a staged fetch. Cheaper than rm_rf, which would fork a shell
// for something this small.
static void discard_temp(const char* dir, const char* file) {
    unlink(file);
    rmdir(dir);
}

// Fetch a URL straight to a file, which is left in place on success and removed
// on failure. The right shape when the body is too big to want in memory, or
// when a parser can read it off disk.
//
// @return  bytes written, or -1 on failure
int wget_fetch_file(const char* url, const char* filepath) {
    if (!url || !filepath) {
        LOG_error("[WgetFetch] Invalid parameters\n");
        return -1;
    }

    char safe_url[4096];
    char safe_path[2048];
    shell_escape(url, safe_url, sizeof(safe_url));
    shell_escape(filepath, safe_path, sizeof(safe_path));

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "curl -sSL --fail %s --connect-timeout 15 --max-time 30 --retry 2"
        " -A \"" HTTP_USER_AGENT "\" -o \"%s\" \"%s\"",
        http_tls_flags(), safe_path, safe_url);

    int ret = system(cmd);
    if (ret != 0) {
        LOG_error("[WgetFetch] Failed to fetch: %s (exit=%d)\n", url, ret);
        unlink(filepath);
        return -1;
    }

    struct stat st;
    if (stat(filepath, &st) != 0 || st.st_size <= 0) {
        LOG_error("[WgetFetch] Empty response for: %s\n", url);
        unlink(filepath);
        return -1;
    }

    return (int)st.st_size;
}

// Fetch a URL into buffer, which must have room for capacity body bytes.
// A body larger than that is refused rather than truncated - a caller handed a
// clipped feed or JSON document parses garbage without knowing it.
//
// @return  bytes read, or -1 on failure
int wget_fetch_bytes(const char* url, uint8_t* buffer, int capacity) {
    if (!buffer || capacity <= 0) {
        LOG_error("[WgetFetch] Invalid parameters\n");
        return -1;
    }

    // Staged through a file rather than read off a pipe: popen from SDL/audio
    // threads has proven unreliable here
    char temp_dir[128];
    if (!mk_tempdir("wget", temp_dir, sizeof(temp_dir))) {
        LOG_error("[WgetFetch] Could not stage a temp file for: %s\n", url);
        return -1;
    }

    char tmpfile[192];
    snprintf(tmpfile, sizeof(tmpfile), "%s/body", temp_dir);

    int size = wget_fetch_file(url, tmpfile);
    if (size < 0) {
        discard_temp(temp_dir, tmpfile);
        return -1;
    }

    if (size > capacity) {
        LOG_error("[WgetFetch] Response too large (%d > %d): %s\n", size, capacity, url);
        discard_temp(temp_dir, tmpfile);
        return -1;
    }

    FILE* fp = fopen(tmpfile, "rb");
    if (!fp) {
        LOG_error("[WgetFetch] Failed to open temp file for: %s\n", url);
        discard_temp(temp_dir, tmpfile);
        return -1;
    }

    int total = (int)fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    discard_temp(temp_dir, tmpfile);

    if (total != size) {
        LOG_error("[WgetFetch] Short read for: %s\n", url);
        return -1;
    }

    return total;
}

int wget_fetch_string(const char* url, char* buffer, int buffer_size) {
    if (!buffer || buffer_size < 1) {
        LOG_error("[WgetFetch] Invalid parameters\n");
        return -1;
    }

    int total = wget_fetch_bytes(url, (uint8_t*)buffer, buffer_size - 1);
    if (total < 0) return -1;

    buffer[total] = '\0';
    return total;
}

long wget_probe_size(const char* url) {
    if (!url) return 0;

    char safe_url[4096];
    shell_escape(url, safe_url, sizeof(safe_url));

    // -s without -S: a failed probe is not worth a log line, the caller copes.
    // Bounded outright, unlike a bulk transfer: this is one HEAD request, and
    // the caller is blocked on it. Redirect chains are real here - a podcast
    // episode can take five hops - but they cost seconds, not minutes.
    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "curl -sIL %s --connect-timeout 10 --max-time 20 -A \"" HTTP_USER_AGENT "\" \"%s\"",
        http_tls_flags(), safe_url);

    FILE* pipe = popen(cmd, "r");
    if (!pipe) return 0;

    long size = 0;
    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            long val = atol(p + 15);
            if (val > 0) size = val;
        }
    }
    pclose(pipe);

    return size;
}

// Start curl writing url to filepath, and hand back its pid. Spawned directly
// rather than through a shell: nothing has to be quoted, and the pid is ours to
// poll and to signal on cancel. stdout and stderr are inherited so -S
// diagnostics reach the app log.
//
// @return  child pid, or -1 if it could not be started
static pid_t spawn_download(const char* url, const char* filepath) {
    pid_t pid = fork();
    if (pid != 0) return pid;

    const char* argv[] = {
        "curl", "-sSL", "--fail",
        "--cacert", CA_BUNDLE_PATH,
        "--connect-timeout", "30",
        "--speed-time", "30",
        "--speed-limit", "512",
        "--retry", "2",
        "-A", HTTP_USER_AGENT,
        "-o", filepath,
        url, NULL
    };
    execvp("curl", (char* const*)argv);
    _exit(127);
}

int wget_download_file(const char* url, const char* filepath,
                       WgetProgressFn on_progress, void* ctx) {
    if (!url || !filepath) {
        LOG_error("[WgetFetch] download: invalid parameters\n");
        return -1;
    }

    pid_t pid = spawn_download(url, filepath);
    if (pid < 0) {
        LOG_error("[WgetFetch] could not start curl for: %s\n", url);
        return -1;
    }

    long prev_size = 0;
    int speed = 0;
    struct timespec prev_time;
    clock_gettime(CLOCK_MONOTONIC, &prev_time);
    struct timespec stall_start = prev_time;
    long stall_size = 0;  // File size when stall tracking started

    struct stat init_st;
    if (stat(filepath, &init_st) == 0) {
        prev_size = init_st.st_size;
        stall_size = init_st.st_size;
    }

    bool running = true;
    bool cancelled = false;
    bool stalled = false;
    int fetch_exit = -1;

    while (running) {
        int status = 0;
        pid_t reaped = waitpid(pid, &status, WNOHANG);
        if (reaped == pid) {
            fetch_exit = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            running = false;
            break;
        }
        if (reaped < 0 && errno != EINTR) {
            // Nothing left to wait for, and no status to judge it by
            LOG_error("[WgetFetch] lost track of curl for: %s\n", url);
            running = false;
            break;
        }

        struct stat st;
        long curr_size = 0;
        if (stat(filepath, &st) == 0) {
            curr_size = st.st_size;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - prev_time.tv_sec) +
                         (now.tv_nsec - prev_time.tv_nsec) / 1e9;

        if (elapsed >= 1.0) {
            long bytes_delta = curr_size - prev_size;
            speed = (int)(bytes_delta / elapsed);
            if (speed < 0) speed = 0;
            prev_size = curr_size;
            prev_time = now;
        }

        if (on_progress && !on_progress(curr_size, speed, ctx)) {
            cancelled = true;
            break;
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
                stalled = true;
                break;
            }
        }

        usleep(200000);  // 200ms polling interval
    }

    if (running) {
        kill(pid, SIGTERM);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
    }

    if (cancelled) {
        unlink(filepath);
        return -1;
    }

    // curl's exit status decides: a file of plausible size proves nothing on its
    // own, since a truncated transfer leaves one too. The partial file stays put
    // either way, and it is the caller's to clean up.
    struct stat st;
    if (stalled || fetch_exit != 0 || stat(filepath, &st) != 0 || st.st_size <= 0) {
        LOG_error("[WgetFetch] download failed: %s\n", url);
        return -1;
    }

    return (int)st.st_size;
}
