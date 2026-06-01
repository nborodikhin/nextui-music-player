// Characterization tests for src/ring_log.c — locks in the ring buffer's
// append/dump/wraparound behavior. Host-compiled (see test/Makefile).

#include "ring_log.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Dump the ring to a temp file and read it back into `out` (capacity `cap`).
// Returns bytes read, or -1 on dump error. Also returns RingLog_dump's count
// via *dumped_len when non-NULL.
static ssize_t dump_to_buf(char* out, size_t cap, ssize_t* dumped_len) {
    FILE* tmp = tmpfile();
    if (!tmp) { perror("tmpfile"); exit(2); }
    int fd = fileno(tmp);

    ssize_t n = RingLog_dump(fd);
    if (dumped_len) *dumped_len = n;
    if (n < 0) { fclose(tmp); return -1; }

    rewind(tmp);
    size_t got = fread(out, 1, cap, tmp);
    fclose(tmp);
    return (ssize_t)got;
}

// Deterministic non-trivial byte pattern (avoids 0 so accidental zero-fill shows).
static unsigned char pat(size_t i) { return (unsigned char)(1 + (i % 251)); }

TEST(empty_dump_is_zero_bytes) {
    RingLog_init();
    char buf[16];
    ssize_t dumped = 0;
    ssize_t got = dump_to_buf(buf, sizeof(buf), &dumped);
    CHECK_EQ_SZ(got, 0);
    CHECK_EQ_SZ(dumped, 0);
    CHECK_EQ_SZ(RingLog_totalAppended(), 0);
}

TEST(simple_append_roundtrips) {
    RingLog_init();
    RingLog_append("hello\n", 6);
    char buf[64];
    ssize_t got = dump_to_buf(buf, sizeof(buf), NULL);
    CHECK_EQ_SZ(got, 6);
    CHECK(memcmp(buf, "hello\n", 6) == 0);
    CHECK_EQ_SZ(RingLog_totalAppended(), 6);
}

TEST(appends_concatenate_in_order) {
    RingLog_init();
    RingLog_append("AAA", 3);
    RingLog_append("BBB", 3);
    RingLog_append("CC", 2);
    char buf[64];
    ssize_t got = dump_to_buf(buf, sizeof(buf), NULL);
    CHECK_EQ_SZ(got, 8);
    CHECK(memcmp(buf, "AAABBBCC", 8) == 0);
    CHECK_EQ_SZ(RingLog_totalAppended(), 8);
}

TEST(null_or_zero_len_is_noop) {
    RingLog_init();
    RingLog_append(NULL, 5);
    RingLog_append("x", 0);
    CHECK_EQ_SZ(RingLog_totalAppended(), 0);
}

TEST(exact_fill_dumps_whole_buffer) {
    RingLog_init();
    char* in = malloc(RING_LOG_SIZE);
    for (size_t i = 0; i < RING_LOG_SIZE; i++) in[i] = (char)pat(i);
    RingLog_append(in, RING_LOG_SIZE);

    CHECK_EQ_SZ(RingLog_totalAppended(), RING_LOG_SIZE);

    char* out = malloc(RING_LOG_SIZE + 16);
    ssize_t dumped = 0;
    ssize_t got = dump_to_buf(out, RING_LOG_SIZE + 16, &dumped);
    CHECK_EQ_SZ(got, RING_LOG_SIZE);
    CHECK_EQ_SZ(dumped, RING_LOG_SIZE);
    CHECK(memcmp(out, in, RING_LOG_SIZE) == 0);
    free(in); free(out);
}

TEST(oversized_append_keeps_trailing_bytes) {
    RingLog_init();
    size_t big = RING_LOG_SIZE + 100;
    char* in = malloc(big);
    for (size_t i = 0; i < big; i++) in[i] = (char)pat(i);
    RingLog_append(in, big);

    // Only the trailing RING_LOG_SIZE bytes are retained; total counts the kept span.
    CHECK_EQ_SZ(RingLog_totalAppended(), RING_LOG_SIZE);

    char* out = malloc(RING_LOG_SIZE + 16);
    ssize_t got = dump_to_buf(out, RING_LOG_SIZE + 16, NULL);
    CHECK_EQ_SZ(got, RING_LOG_SIZE);
    CHECK(memcmp(out, in + 100, RING_LOG_SIZE) == 0);
    free(in); free(out);
}

TEST(wraparound_overwrites_oldest_preserves_order) {
    RingLog_init();
    // Fill exactly, head wraps to 0.
    char* in = malloc(RING_LOG_SIZE);
    for (size_t i = 0; i < RING_LOG_SIZE; i++) in[i] = (char)pat(i);
    RingLog_append(in, RING_LOG_SIZE);
    // Append 3 more — overwrites the 3 oldest bytes at offset 0.
    RingLog_append("XYZ", 3);

    CHECK_EQ_SZ(RingLog_totalAppended(), (size_t)RING_LOG_SIZE + 3);

    char* out = malloc(RING_LOG_SIZE + 16);
    ssize_t got = dump_to_buf(out, RING_LOG_SIZE + 16, NULL);
    // Dump stays RING_LOG_SIZE bytes: oldest 3 are gone, order preserved.
    CHECK_EQ_SZ(got, RING_LOG_SIZE);
    // First SIZE-3 bytes are the original pattern starting at index 3.
    CHECK(memcmp(out, in + 3, RING_LOG_SIZE - 3) == 0);
    // Last 3 bytes are the newest append.
    CHECK(memcmp(out + (RING_LOG_SIZE - 3), "XYZ", 3) == 0);
    free(in); free(out);
}

TEST(init_resets_state) {
    RingLog_init();
    RingLog_append("data", 4);
    CHECK_EQ_SZ(RingLog_totalAppended(), 4);
    RingLog_init();
    CHECK_EQ_SZ(RingLog_totalAppended(), 0);
    char buf[16];
    ssize_t got = dump_to_buf(buf, sizeof(buf), NULL);
    CHECK_EQ_SZ(got, 0);
}

int main(void) {
    printf("ring_log:\n");
    RUN(empty_dump_is_zero_bytes);
    RUN(simple_append_roundtrips);
    RUN(appends_concatenate_in_order);
    RUN(null_or_zero_len_is_noop);
    RUN(exact_fill_dumps_whole_buffer);
    RUN(oversized_append_keeps_trailing_bytes);
    RUN(wraparound_overwrites_oldest_preserves_order);
    RUN(init_resets_state);
    return test_summary();
}
