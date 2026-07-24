// Characterization tests for src/ring_log.c — locks in the ring buffer's
// append/dump/wraparound behavior. Host-compiled (see test/Makefile).

#include "ring_log.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

// ring_log.c is compiled with -Dwrite=rl_test_write for this test (see
// test/Makefile), routing every write() inside RingLog_dump through the mock
// below. It caps each call so short writes are forced deterministically —
// a real socket/pipe with a draining reader completes the whole write and never
// exercises the loop. The old single-write() dump silently drops the remainder
// of each segment under this mock; the fixed write_all loop keeps every byte.
static int rl_short_writes = 0;   // 0 = pass through; >0 = cap each write to this
ssize_t rl_test_write(int fd, const void* buf, size_t n);
ssize_t rl_test_write(int fd, const void* buf, size_t n) {
    if (rl_short_writes > 0 && n > (size_t)rl_short_writes) n = (size_t)rl_short_writes;
    return (ssize_t)syscall(SYS_write, fd, buf, n);
}

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

// Like dump_to_buf, but caps every write inside RingLog_dump to `chunk` bytes
// (via the rl_test_write mock) so the write_all loop runs many iterations.
static ssize_t dump_short(char* out, size_t cap, size_t chunk, ssize_t* dumped_len) {
    FILE* tmp = tmpfile();
    if (!tmp) { perror("tmpfile"); exit(2); }
    int fd = fileno(tmp);

    rl_short_writes = (int)chunk;
    ssize_t n = RingLog_dump(fd);
    rl_short_writes = 0;

    if (dumped_len) *dumped_len = n;
    if (n < 0) { fclose(tmp); return -1; }

    rewind(tmp);
    size_t got = fread(out, 1, cap, tmp);
    fclose(tmp);
    return (ssize_t)got;
}

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

TEST(short_writes_do_not_truncate_unwrapped) {
    RingLog_init();
    // Half-fill so total_appended < RING_LOG_SIZE (the unwrapped dump path),
    // but large enough to overflow the tiny socket buffer many times over.
    size_t len = RING_LOG_SIZE / 2;
    char* in = malloc(len);
    for (size_t i = 0; i < len; i++) in[i] = (char)pat(i);
    RingLog_append(in, len);

    char* out = malloc(len + 16);
    ssize_t dumped = 0;
    ssize_t got = dump_short(out, len + 16, 4096, &dumped);
    CHECK_EQ_SZ(got, len);       // every byte arrived despite short writes
    CHECK_EQ_SZ(dumped, len);    // and RingLog_dump reported the full count
    CHECK(memcmp(out, in, len) == 0);
    free(in); free(out);
}

TEST(short_writes_preserve_order_when_wrapped) {
    RingLog_init();
    // Fill exactly, then append 3 more so the buffer is wrapped and the dump
    // takes the two-segment path — the case the old code could splice in the
    // wrong order after a short write on the first segment.
    char* in = malloc(RING_LOG_SIZE);
    for (size_t i = 0; i < RING_LOG_SIZE; i++) in[i] = (char)pat(i);
    RingLog_append(in, RING_LOG_SIZE);
    RingLog_append("XYZ", 3);

    char* out = malloc(RING_LOG_SIZE + 16);
    ssize_t dumped = 0;
    // Cap at 4000 so the first segment (RING_LOG_SIZE - 3 bytes) needs several
    // writes and a short write lands mid-segment, before the second segment.
    ssize_t got = dump_short(out, RING_LOG_SIZE + 16, 4000, &dumped);
    CHECK_EQ_SZ(got, RING_LOG_SIZE);
    CHECK_EQ_SZ(dumped, RING_LOG_SIZE);
    // Oldest 3 gone, original pattern from index 3, newest append at the tail.
    CHECK(memcmp(out, in + 3, RING_LOG_SIZE - 3) == 0);
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
    RUN(short_writes_do_not_truncate_unwrapped);
    RUN(short_writes_preserve_order_when_wrapped);
    RUN(init_resets_state);
    return test_summary();
}
