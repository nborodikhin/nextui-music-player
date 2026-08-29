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
