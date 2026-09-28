#include "debug.h"

#ifdef DEBUG

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static struct timespec start_time;

void Debug_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
}

DebugTime Debug_time(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (DebugTime)(now.tv_sec - start_time.tv_sec) * 1000 +
           (now.tv_nsec - start_time.tv_nsec) / 1000000;
}

DebugTime Debug_msSince(DebugTime time) {
    return Debug_time() - time;
}

void Debug_log(const char* fmt, ...) {
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    DebugTime ms = Debug_time();
    fprintf(stderr, "[+%02lld.%03lld] %s", ms / 1000, ms % 1000, message);
    fflush(stderr);
}

#endif
