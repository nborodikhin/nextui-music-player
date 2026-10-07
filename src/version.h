#ifndef VERSION_H
#define VERSION_H

#include <stdbool.h>

// The version of the app. The build generates its definition.
extern const char APP_VERSION[];

// Returns true for a semantic version with an optional leading v.
bool Version_isValid(const char* version);

// Returns the version order. Invalid versions are equal and below valid ones.
int Version_compare(const char* left, const char* right);

#endif
