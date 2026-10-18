#include "defines.h"
#include "api.h"
#include "pad_reset.h"

static uint32_t reset_count = 0;

void PadReset_run(void) {
    PAD_reset();
    PadReset_record();
}

void PadReset_record(void) {
    reset_count++;
}

uint32_t PadReset_getCount(void) {
    return reset_count;
}
