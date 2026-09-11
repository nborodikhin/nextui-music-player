#ifndef __TEST_CONTROL_H__
#define __TEST_CONTROL_H__

#include <stdbool.h>

// External control channel. It reads button commands from a file, a FIFO, a
// file descriptor or stdin, and it writes one reply for each command line.
// The channel stays closed until --test-control gives a value.
//
// The end of a file or of a pipe stops the app, if the script did not give the
// keep command. A FIFO gives no end, thus such a run stops on quit.

// The channel is in the build only where TEST_CONTROL is set, which the debug
// and debuggable kinds do. The release kind gets the stubs below, thus a call
// site needs no condition of its own and the code goes away at compile time.
#ifdef TEST_CONTROL

// value: the text after --test-control=, which is "<in>[,<out>]".
// Returns false if a side cannot be opened. The caller then stops the app.
bool TestControl_init(const char* value);

// Read the commands that arrived, execute the actions that are due, and put
// their buttons into the pad state.
// Call one time in each frame, immediately after PAD_poll().
void TestControl_tick(void);

void TestControl_quit(void);

#else

static inline bool TestControl_init(const char* value) { (void)value; return false; }
static inline void TestControl_tick(void) {}
static inline void TestControl_quit(void) {}

#endif

#endif
