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
// Returns the byte count on a COMPLETE capture (full header + every declared
// row). Returns 0 if uninitialized, if fb0 is unavailable, or if the image is
// incomplete — in which case the caller must discard any file it created, since
// a truncated BMP is unloadable and would be retried on every launch.
ssize_t FbCapture_writeBmp(int fd);

// True once FbCapture_init() has succeeded. Lets the crash handler skip creating
// screen.bmp entirely when capture is unavailable.
bool FbCapture_isAvailable(void);

// Framebuffer dimensions read at init. 0 if FbCapture_init() has not succeeded.
int FbCapture_width(void);
int FbCapture_height(void);
int FbCapture_bpp(void);

#endif
