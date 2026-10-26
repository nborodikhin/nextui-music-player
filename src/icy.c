#include <ctype.h>
#include <string.h>

#include "icy.h"
#include "utf8.h"

// Longest metadata block: the length byte of the protocol counts blocks of 16 bytes
#define ICY_MAX_META 4080

// Replace title with the value of the first attribute tag="..." in it
static bool extract_icy_title_tag(char* title, size_t title_size, const char* tag) {
    size_t tag_len = strlen(tag);
    char* match = title;
    while ((match = strstr(match, tag)) != NULL) {
        if (match == title || isspace((unsigned char)match[-1]) ||
            match[-1] == ',' || match[-1] == ';') {
            char* value = match + tag_len;
            char* value_end = strchr(value, '"');
            if (value_end) {
                size_t value_len = (size_t)(value_end - value);
                if (value_len >= title_size) value_len = title_size - 1;
                memmove(title, value, value_len);
                title[value_len] = '\0';
                return true;
            }
        }
        match += tag_len;
    }
    return false;
}

static void normalize_icy_title(char* title, size_t title_size) {
    if (!strchr(title, '=')) return;
    if (extract_icy_title_tag(title, title_size, "text=\"")) return;
    if (extract_icy_title_tag(title, title_size, "title=\"")) return;
    title[0] = '\0';
}

bool ICY_parseStreamTitle(const uint8_t* meta, size_t len, char* artist, size_t artist_size,
                          char* title, size_t title_size) {
    if (!meta || !artist || artist_size == 0 || !title || title_size == 0) return false;

    // Format: StreamTitle='Artist - Title';StreamUrl='...';
    char block[ICY_MAX_META + 1];
    if (len > ICY_MAX_META) len = ICY_MAX_META;
    memcpy(block, meta, len);
    block[len] = '\0';

    static const char kMark[] = "StreamTitle='";
    char* value = strstr(block, kMark);
    if (!value) return false;
    value += sizeof(kMark) - 1;

    char* value_end = strchr(value, '\'');
    if (!value_end) return false;

    // Split the whole value before a field cuts it: UTF-8 can be longer than the source,
    // and a cut can remove the separator or the end of an attribute
    char decoded[ICY_MAX_META * 2 + 1];
    UTF8_fromUnknown(decoded, sizeof(decoded), (const uint8_t*)value,
                     (size_t)(value_end - value));

    char* song = decoded;
    char* separator = strstr(decoded, " - ");
    if (separator) {
        *separator = '\0';
        UTF8_copy(artist, artist_size, decoded);
        song = separator + 3;
    } else {
        artist[0] = '\0';
    }
    normalize_icy_title(song, sizeof(decoded) - (size_t)(song - decoded));
    UTF8_copy(title, title_size, song);
    return true;
}
