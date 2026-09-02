#ifndef __TEST_CONTROL_H__
#define __TEST_CONTROL_H__

#include <stdbool.h>

// External control channel. It reads button commands from a file, a FIFO, a
// file descriptor or stdin, and it writes one reply for each command line.
// The channel stays closed until --test-control gives a value.
//
// The end of a file or of a pipe stops the app, if the script did not give the
// keep command. A FIFO gives no end, thus such a run stops on quit.

// value: the text after --test-control=, which is "<in>[,<out>]".
// Returns false if a side cannot be opened. The caller then stops the app.
bool TestControl_init(const char* value);

// Read the commands that arrived, execute the actions that are due, and put
// their buttons into the pad state.
// Call one time in each frame, immediately after PAD_poll().
void TestControl_tick(void);

void TestControl_quit(void);

#endif
