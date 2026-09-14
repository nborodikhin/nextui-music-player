#ifndef __FILE_UTILS_H__
#define __FILE_UTILS_H__

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/**
 * Escape a string for use inside double quotes in a shell command line.
 *
 * @param src      String to escape
 * @param dst      Destination buffer, needs room for 2x the length of src
 * @param dst_size Size of dst
 */
void shell_escape(const char* src, char* dst, int dst_size);

/**
 * Make path and each directory above it.
 *
 * @return true if path is a directory when the function returns
 */
bool mkdir_p(const char* path);

/**
 * Return a new path under the user data directory of the app.
 * The caller must free the returned string.
 *
 * @return the path, or NULL if memory allocation fails
 */
char* userdata_path(const char* rel);

/**
 * Write a path under the user data directory of the app to out.
 *
 * @return the result that snprintf() returns for the complete path
 */
int userdata_snpath(const char* rel, char* out, size_t out_size);

/**
 * Make a directory under the user data directory of the app.
 *
 * @return true if the directory exists when the function returns
 */
bool userdata_mkdir(const char* rel);

/**
 * Create a uniquely named directory under /tmp and write its path to out. The
 * name carries the pid and a counter, so neither two threads nor two calls from
 * the same one can land on the same directory.
 *
 * @param prefix   Names the caller, so a stray directory can be traced back
 * @return         true if the directory was created
 */
bool mk_tempdir(const char* prefix, char* out, size_t out_size);

/**
 * Delete path and everything under it.
 */
void rm_rf(const char* path);

/**
 * Called for each file cp_rf() copies, with its path relative to the source root.
 */
typedef void (*CopyProgressFn)(const char* rel_path, void* ctx);

/**
 * Copy the contents of src into dst, recursively, overwriting what is already
 * there and creating directories as needed. File modes are preserved.
 *
 * @param on_file Called after each file lands, may be NULL
 * @param ctx     Passed to on_file
 * @return        true if everything copied
 */
bool cp_rf(const char* src, const char* dst, CopyProgressFn on_file, void* ctx);

/**
 * Called as extract_zip() works through the archive.
 *
 * Note: done and total count all zip entries including directories, so the total
 *       may be larger than the number of files.
 *
 * @param done  Entries processed so far
 * @param total Entries in the archive
 * @param ctx   Caller's context pointer, passed through untouched
 */
typedef void (*ExtractProgressFn)(long done, long total, void* ctx);

/**
 * Unpack a zip into dest_dir, creating directories as needed.
 * Any failure fails the whole extraction.
 *
 * Unix permission bits for files are restored from archive (default 0644),
 * directories are always 0755.
 *
 * @param on_progress Called per entry, may be NULL
 * @return            Files written, or -1 if the archive could not be unpacked
 */
int extract_zip(const char* zip_path, const char* dest_dir,
                ExtractProgressFn on_progress, void* ctx);

/**
 * Recursively look for a file called name under root.
 * Note that it finds _any_ file with that name, not just the top-level one.
 *
 * @return true if found, with the full path written to out
 */
bool find_file(const char* root, const char* name, char* out, size_t out_size);

#endif
