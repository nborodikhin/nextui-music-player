#ifndef __DEFINES_H__
#define __DEFINES_H__
// Host-test stub. The real defines.h pulls in SDL via api.h; most units under
// test (watchdog.c) reference nothing from it.
//
// crash_handler.c needs SHARED_USERDATA_PATH to seed its compile-time default
// bundle root. The value is irrelevant to the tests — test_crash_handler.c
// retargets the root at a temp dir via CrashHandler_setBundleRootForTesting()
// before installing handlers — but it must exist and must never resolve to a
// real path a test could write to.
#ifndef SHARED_USERDATA_PATH
#define SHARED_USERDATA_PATH "/nonexistent-host-test-userdata"
#endif

#endif
