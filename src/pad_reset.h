#ifndef __PAD_RESET_H__
#define __PAD_RESET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Clears the pad state of NextUI and counts the reset. Call it in place of PAD_reset(), thus
// the input driver knows that each held button is gone.
void PadReset_run(void);

// Counts a reset that NextUI did itself, such as the one at a wake from sleep.
void PadReset_record(void);

// Returns the count of resets since the start. The input driver compares it with the count
// that it read in the last frame.
uint32_t PadReset_getCount(void);

#ifdef __cplusplus
}
#endif

#endif
