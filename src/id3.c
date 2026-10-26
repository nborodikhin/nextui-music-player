#include <string.h>

#include "id3.h"
#include "utf8.h"

// The encoding byte of an ID3v2 text frame
enum {
    ID3_ENCODING_LATIN1   = 0,
    ID3_ENCODING_UTF16    = 1,   // with a byte order mark
    ID3_ENCODING_UTF16_BE = 2,
    ID3_ENCODING_UTF8     = 3,
};

static bool is_utf16(uint8_t encoding) {
    return encoding == ID3_ENCODING_UTF16 || encoding == ID3_ENCODING_UTF16_BE;
}

size_t ID3_textToUtf8(char* dst, size_t dst_size, uint8_t encoding, const uint8_t* src,
                      size_t src_len) {
    if (!dst || dst_size == 0) return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    if (!is_utf16(encoding)) return UTF8_fromUnknown(dst, dst_size, src, src_len);

    // Without a byte order mark, Windows taggers write little-endian text
    bool big_endian = (encoding == ID3_ENCODING_UTF16_BE);
    if (src_len >= 2 && src[0] == 0xFE && src[1] == 0xFF) {
        big_endian = true;
        src += 2;
        src_len -= 2;
    } else if (src_len >= 2 && src[0] == 0xFF && src[1] == 0xFE) {
        big_endian = false;
        src += 2;
        src_len -= 2;
    }
    return UTF8_fromUtf16(dst, dst_size, src, src_len, big_endian);
}

size_t ID3_skipText(uint8_t encoding, const uint8_t* src, size_t src_len) {
    if (!src) return 0;

    if (is_utf16(encoding)) {
        for (size_t index = 0; index + 1 < src_len; index += 2) {
            if (src[index] == 0 && src[index + 1] == 0) return index + 2;
        }
        return src_len;
    }

    const uint8_t* end = (const uint8_t*)memchr(src, 0, src_len);
    return end ? (size_t)(end - src) + 1 : src_len;
}

// Offsets and size of the text fields of an ID3v1 tag
#define V1_FIELD_SIZE 30

static size_t v1_field_offset(ID3V1Field field) {
    switch (field) {
        case ID3_V1_TITLE:  return 3;
        case ID3_V1_ARTIST: return 33;
        case ID3_V1_ALBUM:  return 63;
    }
    return 3;
}

static bool is_v1_placeholder(const uint8_t* text, size_t length) {
    bool has_mark = false;
    for (size_t index = 0; index < length && text[index]; index++) {
        if (text[index] == '?') has_mark = true;
        else if (text[index] != ' ') return false;
    }
    return has_mark;
}

bool ID3_readV1Field(char* dst, size_t dst_size, const uint8_t* tag, ID3V1Field field) {
    if (!dst || dst_size == 0 || !tag) return false;
    if (memcmp(tag, "TAG", 3) != 0) return false;

    const uint8_t* text = tag + v1_field_offset(field);
    if (is_v1_placeholder(text, V1_FIELD_SIZE)) return false;

    char decoded[V1_FIELD_SIZE * 2 + 1];
    size_t length = UTF8_fromUnknown(decoded, sizeof(decoded), text, V1_FIELD_SIZE);
    while (length > 0 && decoded[length - 1] == ' ') length--;
    if (length == 0) return false;

    decoded[length] = '\0';
    UTF8_copy(dst, dst_size, decoded);
    return true;
}
