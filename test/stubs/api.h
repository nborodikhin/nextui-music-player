#ifndef __API_H__
#define __API_H__
// Host-test stub. The real api.h includes sdl.h/platform.h/scaler.h/config.h,
// none of which exist (or are wanted) on the host. watchdog.c only needs
// LOG_trace, provided by the genuine src/log_trace.h.
//
// The real api.h defines the LOG_* family before log_trace.h is included, and
// log_trace.h now enforces that order with an #error on LOG_info. Mirror just
// enough of that contract here — no-op level macros — so the guard is satisfied
// when the real log_trace.h is compiled against this stub.
#define LOG_note(level, ...) ((void)0)
#define LOG_debug(...)       ((void)0)
#define LOG_info(...)        ((void)0)
#define LOG_warn(...)        ((void)0)
#define LOG_error(...)       ((void)0)

// crash_handler.c is compiled with -DPLATFORM=... in the real build; mirror a
// default so the host test doesn't have to special-case meta.txt's platform
// line. test_crash_handler.c asserts against this exact value.
#ifndef PLATFORM
#define PLATFORM "hosttest"
#endif

#endif
