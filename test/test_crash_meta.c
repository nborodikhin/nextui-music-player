// Host tests for the pure meta.txt formatter (src/crash_meta.c).
//
// The point of splitting CrashMeta_format() out of crash_handler.c is that the
// exact bytes of meta.txt become testable without a device, SDL, or the player.
// The golden test below is deliberately a full literal: meta.txt is a frozen
// format (see spec/crash-reporting.md), so any change to the key set, their
// order, or their spelling MUST fail here and force an explicit decision.

#include <signal.h>
#include <string.h>

#include "crash_meta.h"
#include "test.h"

// A fully-populated, representative snapshot. Signal number is a literal (not
// SIGSEGV) because the field is passed straight through — the test must not
// depend on the host's signal numbering.
static CrashMeta sample(void) {
    CrashMeta m = {
        .version           = "1.4.2",
        .platform          = "tg5040",
        .signal_name       = "SIGSEGV",
        .signal_number     = 11,
        .uptime_ms         = 123456,
        .last_input_button = "BTN_A",
        .last_input_age_ms = 250,
        .heartbeat_age_ms  = 17,
        .ring_total_bytes  = 65536,
        .screen_width      = 1024,
        .screen_height     = 768,
        .screen_bpp        = 32,
        .audio_state       = "playing",
        .audio_background  = "music",
        .audio_position_ms = 42000,
        .audio_duration_ms = 180000,
        .audio_track       = "/mnt/SDCARD/Music/song.mp3",
    };
    return m;
}

static const char* GOLDEN =
    "version: 1.4.2\n"
    "platform: tg5040\n"
    "signal: SIGSEGV (11)\n"
    "uptime_ms: 123456\n"
    "last_input_button: BTN_A\n"
    "last_input_age_ms: 250\n"
    "heartbeat_age_ms: 17\n"
    "ring_total_bytes: 65536\n"
    "screen_width: 1024\n"
    "screen_height: 768\n"
    "screen_bpp: 32\n"
    "audio_state: playing\n"
    "audio_background: music\n"
    "audio_position_ms: 42000\n"
    "audio_duration_ms: 180000\n"
    "audio_track: /mnt/SDCARD/Music/song.mp3\n";

// The frozen key set, in order. Mirrors spec/crash-reporting.md.
static const char* EXPECTED_KEYS[] = {
    "version", "platform", "signal", "uptime_ms",
    "last_input_button", "last_input_age_ms", "heartbeat_age_ms",
    "ring_total_bytes", "screen_width", "screen_height", "screen_bpp",
    "audio_state", "audio_background", "audio_position_ms",
    "audio_duration_ms", "audio_track",
};
#define EXPECTED_KEY_COUNT (sizeof(EXPECTED_KEYS) / sizeof(EXPECTED_KEYS[0]))

TEST(golden_bytes_are_exact) {
    char buf[2048];
    CrashMeta m = sample();
    size_t n = CrashMeta_format(&m, buf, sizeof(buf));

    CHECK_EQ_SZ(n, strlen(GOLDEN));
    CHECK(strcmp(buf, GOLDEN) == 0);
    if (strcmp(buf, GOLDEN) != 0) {
        printf("    --- got ---\n%s    --- want ---\n%s", buf, GOLDEN);
    }
}

TEST(return_value_is_length_excluding_nul) {
    char buf[2048];
    CrashMeta m = sample();
    size_t n = CrashMeta_format(&m, buf, sizeof(buf));

    CHECK_EQ_SZ(n, strlen(buf));
    CHECK(buf[n] == '\0');
}

// Every line must parse as "key: value" and the keys must appear in the frozen
// order. This is the machine-readable half of the format contract that the
// crash-report tooling depends on.
TEST(every_line_parses_as_key_colon_value_in_frozen_order) {
    char buf[2048];
    CrashMeta m = sample();
    CrashMeta_format(&m, buf, sizeof(buf));

    size_t idx = 0;
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char* sep = strstr(line, ": ");
        CHECK(sep != NULL);
        if (!sep) continue;

        CHECK(idx < EXPECTED_KEY_COUNT);
        if (idx >= EXPECTED_KEY_COUNT) break;

        size_t klen = (size_t)(sep - line);
        CHECK(klen == strlen(EXPECTED_KEYS[idx]));
        CHECK(strncmp(line, EXPECTED_KEYS[idx], klen) == 0);
        if (strncmp(line, EXPECTED_KEYS[idx], klen) != 0) {
            printf("    line %zu: got key '%.*s', want '%s'\n",
                   idx, (int)klen, line, EXPECTED_KEYS[idx]);
        }
        idx++;
    }
    CHECK_EQ_SZ(idx, EXPECTED_KEY_COUNT);
}

TEST(empty_last_input_button_renders_none) {
    char buf[2048];
    CrashMeta m = sample();
    m.last_input_button = "";
    CrashMeta_format(&m, buf, sizeof(buf));

    CHECK(strstr(buf, "last_input_button: none\n") != NULL);
}

TEST(null_last_input_button_renders_none) {
    char buf[2048];
    CrashMeta m = sample();
    m.last_input_button = NULL;
    CrashMeta_format(&m, buf, sizeof(buf));

    CHECK(strstr(buf, "last_input_button: none\n") != NULL);
}

// A crash can happen before any subsystem has reported in. Formatting must not
// fault on NULL borrowed pointers — it renders them as empty values.
TEST(null_string_fields_render_empty_without_faulting) {
    char buf[2048];
    CrashMeta m = { 0 };
    m.signal_number = 6;
    size_t n = CrashMeta_format(&m, buf, sizeof(buf));

    CHECK(n > 0);
    CHECK(strstr(buf, "version: \n") != NULL);
    CHECK(strstr(buf, "platform: \n") != NULL);
    CHECK(strstr(buf, "signal:  (6)\n") != NULL);
    CHECK(strstr(buf, "audio_track: \n") != NULL);
    // The all-zero struct still emits the complete frozen key set.
    for (size_t i = 0; i < EXPECTED_KEY_COUNT; i++) {
        CHECK(strstr(buf, EXPECTED_KEYS[i]) != NULL);
    }
}

// meta_buf in crash_handler.c is a fixed 2048 bytes and audio_track can be ~512
// chars; truncation must stay in-bounds and keep the buffer NUL-terminated.
TEST(truncation_clamps_and_nul_terminates) {
    char buf[32];
    memset(buf, 'X', sizeof(buf));
    CrashMeta m = sample();

    size_t n = CrashMeta_format(&m, buf, sizeof(buf));

    CHECK_EQ_SZ(n, sizeof(buf) - 1);
    CHECK(buf[sizeof(buf) - 1] == '\0');
    CHECK_EQ_SZ(strlen(buf), sizeof(buf) - 1);
    // Truncated output is still a prefix of the golden bytes.
    CHECK(strncmp(buf, GOLDEN, sizeof(buf) - 1) == 0);
}

TEST(zero_size_and_null_out_are_safe) {
    CrashMeta m = sample();
    char buf[8];

    CHECK_EQ_SZ(CrashMeta_format(&m, buf, 0), 0);
    CHECK_EQ_SZ(CrashMeta_format(&m, NULL, sizeof(buf)), 0);
}

TEST(null_meta_yields_empty_string) {
    char buf[8];
    memset(buf, 'X', sizeof(buf));

    CHECK_EQ_SZ(CrashMeta_format(NULL, buf, sizeof(buf)), 0);
    CHECK(buf[0] == '\0');
}

TEST(signal_names_map_to_frozen_spellings) {
    CHECK(strcmp(CrashMeta_signalName(SIGSEGV), "SIGSEGV") == 0);
    CHECK(strcmp(CrashMeta_signalName(SIGABRT), "SIGABRT") == 0);
    CHECK(strcmp(CrashMeta_signalName(SIGBUS),  "SIGBUS")  == 0);
    CHECK(strcmp(CrashMeta_signalName(SIGFPE),  "SIGFPE")  == 0);
    CHECK(strcmp(CrashMeta_signalName(SIGILL),  "SIGILL")  == 0);
    CHECK(strcmp(CrashMeta_signalName(SIGUSR1), "SIGUSR1") == 0);
}

TEST(unknown_signal_falls_back_to_generic_name) {
    CHECK(strcmp(CrashMeta_signalName(SIGUSR2), "SIGNAL") == 0);
    CHECK(strcmp(CrashMeta_signalName(-1), "SIGNAL") == 0);
}

int main(void) {
    printf("crash_meta:\n");
    RUN(golden_bytes_are_exact);
    RUN(return_value_is_length_excluding_nul);
    RUN(every_line_parses_as_key_colon_value_in_frozen_order);
    RUN(empty_last_input_button_renders_none);
    RUN(null_last_input_button_renders_none);
    RUN(null_string_fields_render_empty_without_faulting);
    RUN(truncation_clamps_and_nul_terminates);
    RUN(zero_size_and_null_out_are_safe);
    RUN(null_meta_yields_empty_string);
    RUN(signal_names_map_to_frozen_spellings);
    RUN(unknown_signal_falls_back_to_generic_name);
    return test_summary();
}
