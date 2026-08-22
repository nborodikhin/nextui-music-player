#include "file_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <zip.h>
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

// The permission bits a zip entry asks for, or 0 when the archive does not say.
// Only the low nine survive: setuid, setgid and sticky are not something a
// downloaded archive gets to request.
static mode_t zip_entry_mode(zip_t* za, zip_uint64_t index) {
    zip_uint8_t opsys = 0;
    zip_uint32_t attributes = 0;

    if (zip_file_get_external_attributes(za, index, 0, &opsys, &attributes) != 0) return 0;
    if (opsys != ZIP_OPSYS_UNIX) return 0;

    return (mode_t)((attributes >> 16) & 0777);
}

int extract_zip(const char* zip_path, const char* dest_dir,
                ExtractProgressFn on_progress, void* ctx) {
    int err = 0;
    zip_t* za = zip_open(zip_path, 0, &err);
    if (!za) {
        return -1;
    }

    int written = 0;
    bool ok = true;

    zip_int64_t num_entries = zip_get_num_entries(za, 0);

    // zip_open() has already read the central directory, so walking the names
    // costs no I/O. Counting files up front lets progress be reported against
    // them rather than against entries, which would count directories the
    // caller never sees.
    long total_files = 0;
    for (zip_int64_t i = 0; i < num_entries; i++) {
        const char* name = zip_get_name(za, i, 0);
        if (!name) continue;

        size_t len = strlen(name);
        if (len > 0 && name[len - 1] == '/') continue;

        total_files++;
    }

    // Nothing is processed yet, and the caller may want to say so
    if (on_progress) on_progress(0, total_files, ctx);

    for (zip_int64_t i = 0; ok && i < num_entries; i++) {
        const char* name = zip_get_name(za, i, 0);
        if (!name || !zip_entry_is_contained(name)) {
            ok = false;
            break;
        }

        char full_path[600];
        snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, name);

        // Check if it's a directory
        size_t name_len = strlen(name);
        if (name_len > 0 && name[name_len - 1] == '/') {
            mkpath(full_path, 0755);
        } else {
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
                ok = false;
                break;
            }

            FILE* out = fopen(full_path, "wb");
            if (!out) {
                zip_fclose(zf);
                ok = false;
                break;
            }

            char buf[8192];
            zip_int64_t bytes_read;
            while ((bytes_read = zip_fread(zf, buf, sizeof(buf))) > 0) {
                if (fwrite(buf, 1, (size_t)bytes_read, out) != (size_t)bytes_read) {
                    ok = false;
                    break;
                }
            }
            if (bytes_read < 0) {
                ok = false;
            }

            if (fclose(out) != 0) ok = false;
            zip_fclose(zf);

            if (!ok) break;

            mode_t mode = zip_entry_mode(za, (zip_uint64_t)i);
            if (mode != 0) chmod(full_path, mode);

            written++;

            // Reported once the file is actually on disk, so one that failed is
            // never counted
            if (on_progress) on_progress(written, total_files, ctx);
        }
    }

    zip_close(za);
    return ok ? written : -1;
}
