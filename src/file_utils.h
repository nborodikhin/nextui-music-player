#ifndef __FILE_UTILS_H__
#define __FILE_UTILS_H__

#include <stdbool.h>
#include <stddef.h>

/**
 * Escape a string for use inside double quotes in a shell command line.
 *
 * @param src      String to escape
 * @param dst      Destination buffer, needs room for 2x the length of src
 * @param dst_size Size of dst
 */
void shell_escape(const char* src, char* dst, int dst_size);

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
 * Recursively look for a file called name under root.
 * Note that it finds _any_ file with that name, not just the top-level one.
 *
 * @return true if found, with the full path written to out
 */
bool find_file(const char* root, const char* name, char* out, size_t out_size);

#endif
