#ifndef __ERROR_H__
#define __ERROR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// The store of the errors that the user must see, kept in memory for the life of the process.
// A report does not replace the log line of the error.

typedef enum {
    ERROR_CLASS_DATABASE,
    ERROR_CLASS_FILE_SYSTEM,
    ERROR_CLASS_COUNT,
} ErrorClass;

// Starts the store. Each class keeps up to `capacity` texts.
void Error_init(int capacity);

// Clears the store. A report after this call does nothing.
void Error_quit(void);

// Keeps `text` in `error_class` as UTF-8, unless the class is full or holds the text already.
// Safe on any thread. A report before Error_init() does nothing.
void Error_report(ErrorClass error_class, const char* text);

#ifdef __cplusplus
}

#include <string>
#include <vector>

/** Returns the texts of `errorClass`, in the order of their first report. */
std::vector<std::string> Error_getReports(ErrorClass errorClass);

/** Returns: true when a class holds a text */
bool Error_hasReports();

/**
 * Shows the toast that sends the user to Settings, when a class holds a text. The store decides
 * how often the toast shows. Call it on the UI thread.
 */
void Error_showToast();
#endif

#endif
