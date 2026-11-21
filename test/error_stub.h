#ifndef __ERROR_STUB_H__
#define __ERROR_STUB_H__

// A store of reports for the C tests that link code which calls Error_report(). It keeps each
// report, with no capacity and no check for a repeated text.

#include <stdbool.h>

#include "error.h"

// Removes each report.
void ErrorStub_clear(void);

// Returns the count of reports in `error_class`.
int ErrorStub_count(ErrorClass error_class);

// Returns: true when `error_class` holds a report equal to `text`
bool ErrorStub_has(ErrorClass error_class, const char* text);

#endif
