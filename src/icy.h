#ifndef __ICY_H__
#define __ICY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The text rules of ICY metadata.

// Read the stream title of the ICY metadata block of len bytes at meta into artist
// and title as UTF-8. A stream title of the form "Artist - Title" gives both, and
// another one gives the title only. A title with a text="..." or title="..."
// attribute gives the value of that attribute.
// Returns: false, with artist and title unchanged, when the block has no stream title
bool ICY_parseStreamTitle(const uint8_t* meta, size_t len, char* artist, size_t artist_size,
                          char* title, size_t title_size);

#ifdef __cplusplus
}
#endif

#endif
