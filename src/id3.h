#ifndef __ID3_H__
#define __ID3_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The text rules of ID3 tags.

// Size of an ID3v1 tag at the end of a file
#define ID3_V1_TAG_SIZE 128

typedef enum {
    ID3_V1_TITLE,
    ID3_V1_ARTIST,
    ID3_V1_ALBUM,
} ID3V1Field;

// Decode the text of an ID3v2 frame from src_len bytes at src into dst as UTF-8,
// at most dst_size - 1 bytes, always terminating. `encoding` is the encoding byte
// of the frame. If text of encoding 0 or 3 is valid UTF-8, it is copied as is,
// otherwise it is converted from ISO-8859-1.
// Returns: bytes written, not counting the terminator
size_t ID3_textToUtf8(char* dst, size_t dst_size, uint8_t encoding, const uint8_t* src,
                      size_t src_len);

// Returns the size in bytes of the text at src with its terminator, or src_len when
// the text has no terminator.
size_t ID3_skipText(uint8_t encoding, const uint8_t* src, size_t src_len);

// Decode one field of the ID3_V1_TAG_SIZE bytes at tag into dst as UTF-8, with no
// trailing spaces. A field that holds only '?' and spaces holds no text: a tagger
// writes '?' for each character that ISO-8859-1 cannot hold.
// Returns: false, with dst unchanged, when the tag has no "TAG" mark or the field
// holds no text
bool ID3_readV1Field(char* dst, size_t dst_size, const uint8_t* tag, ID3V1Field field);

#ifdef __cplusplus
}
#endif

#endif
