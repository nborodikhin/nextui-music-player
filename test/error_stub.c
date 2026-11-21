#include "error_stub.h"

#include <string.h>

#define ERROR_STUB_MAX  64
#define ERROR_STUB_TEXT 512

static char reports[ERROR_CLASS_COUNT][ERROR_STUB_MAX][ERROR_STUB_TEXT];
static int counts[ERROR_CLASS_COUNT];

void Error_init(int capacity) {
    (void)capacity;
    ErrorStub_clear();
}

void Error_quit(void) {
    ErrorStub_clear();
}

void Error_report(ErrorClass error_class, const char* text) {
    if (error_class < 0 || error_class >= ERROR_CLASS_COUNT || !text) return;
    if (counts[error_class] >= ERROR_STUB_MAX) return;
    char* slot = reports[error_class][counts[error_class]++];
    strncpy(slot, text, ERROR_STUB_TEXT - 1);
    slot[ERROR_STUB_TEXT - 1] = '\0';
}

void ErrorStub_clear(void) {
    memset(counts, 0, sizeof(counts));
}

int ErrorStub_count(ErrorClass error_class) {
    return counts[error_class];
}

bool ErrorStub_has(ErrorClass error_class, const char* text) {
    for (int i = 0; i < counts[error_class]; i++) {
        if (strcmp(reports[error_class][i], text) == 0) return true;
    }
    return false;
}
