#ifndef __FB_CAPTURE_H__
#define __FB_CAPTURE_H__

#include <stdbool.h>
#include <sys/types.h>

// Framebuffer-to-BMP capture used by the crash handler.
// See spec/crash-reporting.md.
//
// Init queries /dev/fb0 via FBIOGET_VSCREENINFO / FBIOGET_FSCREENINFO for the
// real geometry, bit depth, and line_length, then pre-builds the 122-byte
// BITMAPV4HEADER. At crash time, FbCapture_writeBmp() opens /dev/fb0, re-reads
// the pan offset from /sys/class/graphics/fb0/pan, writes the pre-built header,
// then streams scanlines into fd (skipping any stride padding).

// Initialize: query framebuffer geometry/depth/stride via ioctl, build the BMP
// header. Idempotent. Returns true only when capture is possible — false if
// /dev/fb0 or the ioctls fail, the dimensions are out of range, or the panel is
// not 32bpp (the BMP writer emits BGRA32). On false, FbCapture_writeBmp is a
// no-op, but FbCapture_bpp()/width()/height() may still report what was read.
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

// Framebuffer geometry and depth read at init (via ioctl). width/height are 0
// until the ioctls succeed. FbCapture_bpp() reports the panel's real
// bits_per_pixel — which may be non-32 even when capture is unavailable — so
// meta.txt can record it; it is 0 only if the query never ran.
int FbCapture_width(void);
int FbCapture_height(void);
int FbCapture_bpp(void);

#endif
