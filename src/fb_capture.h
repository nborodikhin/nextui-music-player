#ifndef __FB_CAPTURE_H__
#define __FB_CAPTURE_H__

#include <stdbool.h>
#include <sys/types.h>

// Framebuffer-to-BMP capture used by the crash handler.
// See spec/crash-reporting.md.
//
// Init reads /sys/class/graphics/fb0/modes to size the BMP header and
// scanline buffer, then pre-builds the 122-byte BITMAPV4HEADER. At crash
// time, FbCapture_writeBmp() opens /dev/fb0, re-reads the pan offset,
// writes the pre-built header, then streams scanlines into fd.

// Initialize: read framebuffer dimensions, allocate static scratch, build
// the BMP header. Idempotent. Returns true on success.
// On false, FbCapture_writeBmp is a no-op.
bool FbCapture_init(void);

// Write the currently visible page of /dev/fb0 to fd as a top-down 32-bit
// BGRA BMP. Async-signal-safe: open/read/write/lseek/close + a hand-rolled
// integer parser for the pan offset. No malloc, no printf.
// Returns bytes written, 0 if uninitialized or fb0 unavailable.
ssize_t FbCapture_writeBmp(int fd);

// Framebuffer dimensions read at init. 0 if FbCapture_init() has not succeeded.
int FbCapture_width(void);
int FbCapture_height(void);
int FbCapture_bpp(void);

#endif
