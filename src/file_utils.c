#include "file_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

void shell_escape(const char* src, char* dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        char c = src[i];
        // The only four the shell still reads inside double quotes
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            dst[j++] = '\\';
        }
        dst[j++] = c;
    }
    dst[j] = '\0';
}

// Distinguishes concurrent temp directories from each other: the pid alone is
// not enough, since several threads stage downloads at once
static volatile int tempdir_seq = 0;

bool mk_tempdir(const char* prefix, char* out, size_t out_size) {
    // pid and counter alone repeat: the counter restarts at zero every launch
    // and the kernel reuses pids, so a directory left behind by a crashed run
    // can carry the same name. The clock breaks that tie.
    snprintf(out, out_size, "/tmp/%s_%d_%ld_%d", prefix, getpid(),
             (long)time(NULL), __sync_fetch_and_add(&tempdir_seq, 1));

    // An existing directory is not ours to use - staging into someone else's
    // leftovers would mix their files into whatever we are about to install
    if (mkdir(out, 0755) != 0) {
        out[0] = '\0';
        return false;
    }

    return true;
}

void rm_rf(const char* path) {
    char safe_path[2048];
    shell_escape(path, safe_path, sizeof(safe_path));

    char cmd[2200];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", safe_path);
    system(cmd);
}

// Copy one file, mode and all. Written beside the destination and renamed over
// it, which is atomic and - the reason it matters here - the only way to replace
// a file that is currently being executed. Opening a running binary for writing
// fails with ETXTBSY; renaming onto its path does not, because the running
// process keeps the old inode.
static bool copy_file(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return false;

    struct stat st;
    if (fstat(in, &st) != 0) {
        close(in);
        return false;
    }

    char staged[700];
    snprintf(staged, sizeof(staged), "%s.new", dst);

    int out = open(staged, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (out < 0) {
        close(in);
        return false;
    }

    bool ok = true;
    char buf[65536];
    ssize_t n;

    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            off += w;
        }
        if (!ok) break;
    }
    if (n < 0) ok = false;

    close(in);
    if (close(out) != 0) ok = false;

    if (ok) {
        // O_CREAT leaves an existing file's old mode alone
        chmod(staged, st.st_mode & 0777);
        ok = rename(staged, dst) == 0;
    }

    if (!ok) unlink(staged);

    return ok;
}

static bool copy_tree(const char* src, const char* dst, const char* rel,
                      CopyProgressFn on_file, void* ctx) {
    DIR* dir = opendir(src);
    if (!dir) return false;

    mkdir(dst, 0755);

    bool ok = true;
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char src_path[600], dst_path[600], rel_path[600];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        snprintf(rel_path, sizeof(rel_path), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);

        if (entry->d_type == DT_DIR) {
            if (!copy_tree(src_path, dst_path, rel_path, on_file, ctx)) ok = false;
        } else if (copy_file(src_path, dst_path)) {
            if (on_file) on_file(rel_path, ctx);
        } else {
            ok = false;
        }
    }

    closedir(dir);
    return ok;
}

bool cp_rf(const char* src, const char* dst, CopyProgressFn on_file, void* ctx) {
    return copy_tree(src, dst, "", on_file, ctx);
}

bool find_file(const char* root, const char* name, char* out, size_t out_size) {
    DIR* dir = opendir(root);
    if (!dir) return false;

    bool found = false;
    struct dirent* entry;

    while (!found && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char path[600];
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);

        if (entry->d_type == DT_DIR) {
            found = find_file(path, name, out, out_size);
        } else if (strcmp(entry->d_name, name) == 0) {
            snprintf(out, out_size, "%s", path);
            found = true;
        }
    }

    closedir(dir);
    return found;
}
