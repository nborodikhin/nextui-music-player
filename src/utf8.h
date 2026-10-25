#ifndef __UTF8_H__
#define __UTF8_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Longest UTF-8 character plus its terminator
#define UTF8_CHAR_SIZE 5

/**
 * Bytes in the character starting at c, or 0 when the sequence is malformed:
 * a bad lead byte, a missing continuation byte, or one cut short by the
 * terminator. Text arriving from a file or another program can be either.
 */
int UTF8_charBytes(const char* c);

/**
 * The codepoint of the character starting at c, or 0 when the sequence is
 * malformed - which is also what a NUL byte gives, since neither can be typed.
 */
unsigned int UTF8_codepoint(const char* c);

/**
 * True for a combining mark - a character that has no meaning without the one
 * it attaches to (the accent in `o` + U+0300).
 */
bool UTF8_isCombining(const char* c);

/**
 * Copy whole characters from src into dst, at most dst_size - 1 bytes, always
 * terminating. A character that does not fit is left out rather than cut, a
 * malformed sequence ends the copy, and a combining mark is dropped when the
 * character it belongs to is not there - so the result is always valid UTF-8.
 *
 * @return bytes written, not counting the terminator
 */
size_t UTF8_copy(char* dst, size_t dst_size, const char* src);

/**
 * Bytes of the last whole character of text, or 0 when it is empty. Deleting
 * that many bytes removes one character, marks included.
 */
size_t UTF8_lastCharBytes(const char* text);

/**
 * Decode UTF-16 text from src_len bytes at src into dst as UTF-8, at most
 * dst_size - 1 bytes, always terminating. The text ends at the first NUL code
 * unit. Pass true in big_endian for big-endian code units.
 *
 * @return bytes written, not counting the terminator
 */
size_t UTF8_fromUtf16(char* dst, size_t dst_size, const uint8_t* src, size_t src_len,
                      bool big_endian);

/**
 * Copy text with no declared charset from src into dst as UTF-8, at most
 * dst_size - 1 bytes, always terminating. The text ends at the first NUL byte or
 * after src_len bytes. If the whole text is valid UTF-8, it is copied as is,
 * otherwise it is converted from ISO-8859-1.
 *
 * @return bytes written, not counting the terminator
 */
size_t UTF8_fromUnknown(char* dst, size_t dst_size, const uint8_t* src, size_t src_len);

#ifdef __cplusplus
}
#endif

#endif
