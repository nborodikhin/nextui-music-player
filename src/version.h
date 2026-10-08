#ifndef VERSION_H
#define VERSION_H

#include <stdbool.h>

#include "include/parson/parson.h"

// The version of the app. The build generates its definition.
extern const char APP_VERSION[];

// Returns true for a semantic version with an optional leading v.
bool Version_isValid(const char* version);

// Returns the version order. Invalid versions are equal and below valid ones.
int Version_compare(const char* left, const char* right);

// Whether a list of releases holds a release to select.
typedef enum {
    VERSION_RELEASE_FOUND,
    VERSION_RELEASE_NO_VERSION,  // no release has a valid tag
    VERSION_RELEASE_NO_ASSET,    // a release has a valid tag, but none has the asset
} VersionReleaseStatus;

// releases is one release object or an array of them, as GitHub returns. A
// draft, a tag that is not a valid version and a release without the asset
// named asset_name are skipped.
VersionReleaseStatus Version_bestReleaseExists(const JSON_Value* releases, const char* asset_name);

// Returns the release of the highest version that has the asset, or NULL. The
// object belongs to releases.
const JSON_Object* Version_getBestRelease(const JSON_Value* releases, const char* asset_name);

// Returns the download URL of the asset named asset_name in the release, or NULL.
const char* Version_getAssetUrl(const JSON_Object* release, const char* asset_name);

#endif
