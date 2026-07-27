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

// DESKTOP ONLY — capture from an SDL-managed software framebuffer instead of
// /dev/fb0. On a desktop host /dev/fb0 is either absent or, when present (e.g.
// `i915drmfb` under X11), reads back as all-zero: the legacy fbdev mapping is
// not the live scanout, since the compositor drives DRM planes. Either way the
// on-device capture path yields nothing useful on a host, which makes the
// screenshot half of a crash bundle untestable there.
//
// `pixels` must point at a 32-bit surface whose in-memory byte order is B,G,R,A
// (SDL_PIXELFORMAT_ARGB8888 on little-endian) — that is what the BMP writer
// emits, so no conversion is needed. `pitch` is the surface's row stride in
// bytes and may exceed width*4.
//
// The buffer is borrowed, never copied or freed, and is read directly from the
// signal handler. That is a plain memory read of a malloc'd buffer — no SDL
// call — so it stays async-signal-safe. A torn read mid-render is possible and
// acceptable, the same trade-off the /dev/fb0 path already makes.
//
// Call after GFX_init() and before CrashHandler_init(); it takes precedence
// over the /dev/fb0 path and makes FbCapture_init() a no-op. Returns false
// (changing nothing) if the arguments are implausible.
bool FbCapture_useSurface(const void* pixels, int width, int height, int pitch);

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
