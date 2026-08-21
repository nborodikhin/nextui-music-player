#ifndef WGET_FETCH_H
#define WGET_FETCH_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Fetch URL content to a file, left in place on success and removed on failure.
 * For bodies too large to want in memory, or that a parser can read off disk.
 * For a big transfer the user is watching, use wget_download_file() instead.
 *
 * @param url      The URL to fetch
 * @param filepath Local path to write
 * @return         Bytes written on success, -1 on failure
 */
int wget_fetch_file(const char* url, const char* filepath);

/**
 * Fetch URL content into memory. Shells out to curl; the wget in the name is
 * historical, kept so callers did not have to churn.
 * A body that does not fit is an error, not a truncation.
 *
 * @param url         The URL to fetch
 * @param buffer      Buffer to store the response body
 * @param buffer_size Size of buffer
 * @return            Bytes read on success, -1 on failure
 */
int wget_fetch_bytes(const char* url, uint8_t* buffer, int buffer_size);

/**
 * Same, for text: the body is NUL-terminated, so at most buffer_size - 1 bytes
 * of it fit. Callers parsing JSON or XML want this one.
 *
 * @param url         The URL to fetch
 * @param buffer      Buffer to store the response body
 * @param buffer_size Size of buffer, terminator included
 * @return            Bytes read, not counting the terminator, or -1 on failure
 */
int wget_fetch_string(const char* url, char* buffer, int buffer_size);

/**
 * curl flags that turn on certificate verification, for callers building their
 * own command lines. The firmware ships no CA store, so verification works only
 * against the bundle in res/, which is part of the pak. There is no unverified
 * fallback: if the bundle is missing the install is broken and every transfer
 * fails, which is the right outcome when what we fetch gets executed.
 *
 * @return  Flag string owned by this module, valid for the process lifetime
 */
const char* http_tls_flags(void);

/**
 * Ask the server how large a URL is, with a HEAD request. The last
 * Content-Length wins, so a redirect chain reports what the CDN will serve
 * rather than the size of a redirect page.
 *
 * Blocks until the server answers - call it from a worker thread.
 *
 * @param url  The URL to probe
 * @return     Size in bytes, or 0 if the server did not say
 */
long wget_probe_size(const char* url);

/**
 * Called by wget_download_file() once per poll, whether or not anything moved.
 * Percentages and time estimates are the caller's business: it knows the total
 * size, having probed for it.
 *
 * @param downloaded  Bytes written so far
 * @param speed_bps   Current rate in bytes/sec, 0 until the first second is up
 * @param ctx         Caller's context pointer, passed through untouched
 * @return            true to let the transfer continue, false to cancel it
 */
typedef bool (*WgetProgressFn)(long downloaded, int speed_bps, void* ctx);

/**
 * Download URL to file. A failed transfer leaves whatever arrived on disk for the
 * caller to deal with; a cancelled one removes it.
 *
 * @param url         The URL to download
 * @param filepath    Local path to save the file
 * @param on_progress Progress callback, may be NULL
 * @param ctx         Passed to on_progress
 * @return            Bytes downloaded on success, -1 on failure or cancellation
 */
int wget_download_file(const char* url, const char* filepath,
                       WgetProgressFn on_progress, void* ctx);

#endif // WGET_FETCH_H
