#include <string.h>

#include "utf8.h"

// Ranges holding nothing but combining marks
static bool is_combining_code(unsigned int code) {
    return (code >= 0x0300 && code <= 0x036F) ||   // diacritics
           (code >= 0x1AB0 && code <= 0x1AFF) ||   // diacritics extended
           (code >= 0x1DC0 && code <= 0x1DFF) ||   // diacritics supplement
           (code >= 0x20D0 && code <= 0x20FF) ||   // marks for symbols
           (code >= 0xFE20 && code <= 0xFE2F);     // half marks
}

// Bytes the lead byte promises, or 0 when it is not a lead byte
static int lead_bytes(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

int UTF8_charBytes(const char* c) {
    if (!c || *c == '\0') return 0;

    int length = lead_bytes((unsigned char)*c);
    if (length == 0) return 0;

    // The promised continuation bytes have to actually be there
    for (int index = 1; index < length; index++) {
        if (((unsigned char)c[index] & 0xC0) != 0x80) return 0;
    }
    return length;
}

// The codepoint of a character already known to be well formed
static unsigned int codepoint(const char* c, int length) {
    unsigned char lead = (unsigned char)*c;

    unsigned int code = (length == 1) ? lead
                      : (length == 2) ? (lead & 0x1Fu)
                      : (length == 3) ? (lead & 0x0Fu)
                                      : (lead & 0x07u);
    for (int index = 1; index < length; index++) {
        code = (code << 6) | ((unsigned char)c[index] & 0x3Fu);
    }
    return code;
}

unsigned int UTF8_codepoint(const char* c) {
    int length = UTF8_charBytes(c);
    if (length == 0) return 0;

    return codepoint(c, length);
}

bool UTF8_isCombining(const char* c) {
    int length = UTF8_charBytes(c);
    if (length < 2) return false;

    return is_combining_code(codepoint(c, length));
}

size_t UTF8_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return 0;

    dst[0] = '\0';
    if (!src) return 0;

    size_t written = 0;
    // Where the character the marks attach to began, so a copy that stops
    // mid-cluster drops the marks with it rather than leaving them orphaned
    size_t cluster = 0;

    for (const char* c = src; *c; ) {
        int length = UTF8_charBytes(c);
        if (length == 0) break;

        bool combining = (length >= 2) && is_combining_code(codepoint(c, length));
        if (combining && written == 0) {
            // Nothing to attach to; the mark alone means nothing
            c += length;
            continue;
        }

        if (written + (size_t)length + 1 > dst_size) {
            if (combining) written = cluster;
            break;
        }

        if (!combining) cluster = written;
        memcpy(dst + written, c, (size_t)length);
        written += (size_t)length;
        c += length;
    }

    dst[written] = '\0';
    return written;
}

size_t UTF8_lastCharBytes(const char* text) {
    if (!text) return 0;

    size_t length = strlen(text);
    if (length == 0) return 0;

    // Walk back over continuation bytes to the character's own lead byte, and
    // over any marks sitting on it
    size_t start = length;
    while (start > 0) {
        start--;
        while (start > 0 && ((unsigned char)text[start] & 0xC0) == 0x80) start--;

        int bytes = UTF8_charBytes(text + start);
        if (bytes == 0) break;
        if (!UTF8_isCombining(text + start)) break;
    }
    return length - start;
}

// Append the UTF-8 form of code at dst + *written. Returns false, and writes
// nothing, when the character and the terminator do not fit.
static bool append_codepoint(char* dst, size_t dst_size, size_t* written, unsigned int code) {
    char bytes[4];
    size_t length;
    if (code < 0x80) {
        bytes[0] = (char)code;
        length = 1;
    } else if (code < 0x800) {
        bytes[0] = (char)(0xC0 | (code >> 6));
        bytes[1] = (char)(0x80 | (code & 0x3F));
        length = 2;
    } else if (code < 0x10000) {
        bytes[0] = (char)(0xE0 | (code >> 12));
        bytes[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (code & 0x3F));
        length = 3;
    } else {
        bytes[0] = (char)(0xF0 | (code >> 18));
        bytes[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (code & 0x3F));
        length = 4;
    }

    if (*written + length + 1 > dst_size) return false;
    memcpy(dst + *written, bytes, length);
    *written += length;
    return true;
}

static unsigned int read_utf16_unit(const uint8_t* unit, bool big_endian) {
    return big_endian ? ((unsigned int)unit[0] << 8 | unit[1])
                      : ((unsigned int)unit[1] << 8 | unit[0]);
}

static bool is_surrogate(unsigned int code) {
    return code >= 0xD800 && code <= 0xDFFF;
}

size_t UTF8_fromUtf16(char* dst, size_t dst_size, const uint8_t* src, size_t src_len,
                      bool big_endian) {
    if (!dst || dst_size == 0) return 0;

    dst[0] = '\0';
    if (!src) return 0;

    size_t written = 0;
    size_t index = 0;
    while (index + 1 < src_len) {
        unsigned int code = read_utf16_unit(src + index, big_endian);
        index += 2;
        if (code == 0) break;

        if (code >= 0xD800 && code <= 0xDBFF && index + 1 < src_len) {
            unsigned int low = read_utf16_unit(src + index, big_endian);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                index += 2;
            }
        }
        if (is_surrogate(code)) code = 0xFFFD;

        if (!append_codepoint(dst, dst_size, &written, code)) break;
    }

    dst[written] = '\0';
    return written;
}

// Bytes of the well-formed character at text, or 0 when it is malformed. Unlike
// UTF8_charBytes(), this also rejects overlong forms, surrogates and codepoints
// above U+10FFFF.
static size_t valid_char_bytes(const uint8_t* text, size_t length) {
    size_t bytes = (size_t)lead_bytes(text[0]);
    if (bytes == 0 || bytes > length) return 0;
    if (bytes == 1) return 1;

    for (size_t index = 1; index < bytes; index++) {
        if ((text[index] & 0xC0) != 0x80) return 0;
    }

    unsigned int code = codepoint((const char*)text, (int)bytes);
    unsigned int shortest = (bytes == 2) ? 0x80 : (bytes == 3) ? 0x800 : 0x10000;
    if (code < shortest || code > 0x10FFFF || is_surrogate(code)) return 0;
    return bytes;
}

static bool is_valid_utf8(const uint8_t* text, size_t length) {
    size_t index = 0;
    while (index < length) {
        size_t bytes = valid_char_bytes(text + index, length - index);
        if (bytes == 0) return false;
        index += bytes;
    }
    return true;
}

size_t UTF8_fromUnknown(char* dst, size_t dst_size, const uint8_t* src, size_t src_len) {
    if (!dst || dst_size == 0) return 0;

    dst[0] = '\0';
    if (!src) return 0;

    const uint8_t* end = (const uint8_t*)memchr(src, 0, src_len);
    size_t length = end ? (size_t)(end - src) : src_len;

    size_t written = 0;
    if (is_valid_utf8(src, length)) {
        size_t index = 0;
        while (index < length) {
            size_t bytes = valid_char_bytes(src + index, length - index);
            if (written + bytes + 1 > dst_size) break;
            memcpy(dst + written, src + index, bytes);
            written += bytes;
            index += bytes;
        }
    } else {
        for (size_t index = 0; index < length; index++) {
            if (!append_codepoint(dst, dst_size, &written, src[index])) break;
        }
    }

    dst[written] = '\0';
    return written;
}
