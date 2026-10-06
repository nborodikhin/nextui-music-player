#ifndef VERSION_ORDER_H
#define VERSION_ORDER_H

#include <stdbool.h>

// Versions are semver (https://semver.org/) with optional prefix 'v'.
// Prefix does not count for validity and comparison.

// Returns true for a semantic version with an optional leading v.
bool Version_isValid(const char* version);

// Returns the version order. Invalid versions are equal and below valid ones.
int Version_compare(const char* left, const char* right);

#endif
