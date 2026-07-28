// fb_capture.c — async-signal-safe framebuffer-to-BMP writer.
//
// BMP layout: 14-byte file header + 108-byte BITMAPV4HEADER (BI_BITFIELDS with
// explicit BGRA channel masks). Negative biHeight makes the BMP top-down so
// scanlines can be streamed in their natural order from /dev/fb0.

#include "fb_capture.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BMP_HEADER_SIZE       122u   // 14 + 108
#define DIB_HEADER_SIZE       108u   // BITMAPV4HEADER
#define BMP_BPP               32u
#define BMP_BYTES_PER_PIXEL   (BMP_BPP / 8u)

#define MAX_WIDTH  4096
#define MAX_HEIGHT 4096

static int fb_width = 0;
static int fb_height = 0;
static int fb_bpp = 0;         // actual panel bits-per-pixel, for meta.txt
static size_t fb_stride = 0;   // bytes per physical row (fb line_length, may be padded)
static size_t fb_row_bytes = 0;// visible bytes per row written to the BMP (w * 4)
static int initialized = 0;

static uint8_t bmp_header[BMP_HEADER_SIZE];

// Desktop only: when non-NULL, scanlines come from this borrowed SDL software
// surface instead of /dev/fb0. See FbCapture_useSurface() in the header for why
// the host cannot use the fbdev path, and why reading this from the signal
// handler is safe.
static const uint8_t* surface_pixels = NULL;

// Scanline scratch — one row, allocated by the loader at app start.
static uint8_t scanline[MAX_WIDTH * BMP_BYTES_PER_PIXEL];

// Little-endian writers (used at init only, not in the handler).
static void put_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static void put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static void put_i32(uint8_t* p, int32_t v) {
    put_u32(p, (uint32_t)v);
}

static void build_header(int w, int h) {
    memset(bmp_header, 0, sizeof(bmp_header));

    uint32_t image_size = (uint32_t)w * (uint32_t)h * BMP_BYTES_PER_PIXEL;
    uint32_t file_size  = BMP_HEADER_SIZE + image_size;

    // BMP file header (14 bytes)
    bmp_header[0] = 'B';
    bmp_header[1] = 'M';
    put_u32(bmp_header + 2,  file_size);          // file size
    put_u32(bmp_header + 6,  0);                  // reserved
    put_u32(bmp_header + 10, BMP_HEADER_SIZE);    // pixel data offset

    // BITMAPV4HEADER (108 bytes), starts at offset 14
    uint8_t* dib = bmp_header + 14;
    put_u32(dib + 0,  DIB_HEADER_SIZE);           // header size
    put_i32(dib + 4,  w);                         // width
    put_i32(dib + 8,  -h);                        // negative -> top-down
    put_u16(dib + 12, 1);                         // planes
    put_u16(dib + 14, BMP_BPP);                   // bits per pixel
    put_u32(dib + 16, 3);                         // BI_BITFIELDS
    put_u32(dib + 20, image_size);                // image size
    put_u32(dib + 24, 2835);                      // x pixels/meter (~72 dpi)
    put_u32(dib + 28, 2835);                      // y pixels/meter
    put_u32(dib + 32, 0);                         // colors used
    put_u32(dib + 36, 0);                         // important colors

    // BGRA channel masks (pixel byte order in memory: B,G,R,A — as little-endian
    // DWORD: 0xAARRGGBB).
    put_u32(dib + 40, 0x00FF0000);                // red   mask
    put_u32(dib + 44, 0x0000FF00);                // green mask
    put_u32(dib + 48, 0x000000FF);                // blue  mask
    put_u32(dib + 52, 0xFF000000);                // alpha mask

    put_u32(dib + 56, 0x73524742);                // CSType = 'sRGB' (LCS_sRGB)
    // Endpoints (36) + gamma R/G/B (12) left zero.
}

bool FbCapture_useSurface(const void* pixels, int width, int height, int pitch) {
    if (!pixels) return false;
    if (width <= 0 || height <= 0 || width > MAX_WIDTH || height > MAX_HEIGHT) return false;

    size_t row_bytes = (size_t)width * BMP_BYTES_PER_PIXEL;
    if (pitch < 0 || (size_t)pitch < row_bytes) return false;   // would read past each row

    surface_pixels = (const uint8_t*)pixels;
    fb_width     = width;
    fb_height    = height;
    fb_bpp       = (int)BMP_BPP;   // the surface is 32bpp by contract
    fb_row_bytes = row_bytes;
    fb_stride    = (size_t)pitch;

    build_header(width, height);
    initialized = 1;
    return true;
}

bool FbCapture_init(void) {
    if (initialized) return true;

    // Query the framebuffer authoritatively via ioctls rather than parsing
    // /sys/.../modes and assuming stride == w*4. FBIOGET_VSCREENINFO gives the
    // real geometry and bit depth; FBIOGET_FSCREENINFO gives line_length, which
    // may be padded beyond w*4 for alignment (a wrong assumption would shear the
    // screenshot diagonally). Runs at init, so ioctl's signal-unsafety is moot.
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0) return false;

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
        close(fd);
        return false;
    }
    close(fd);

    int w = (int)vinfo.xres;
    int h = (int)vinfo.yres;
    fb_bpp = (int)vinfo.bits_per_pixel;   // recorded even if we can't capture

    if (w <= 0 || h <= 0 || w > MAX_WIDTH || h > MAX_HEIGHT) {
        return false;
    }
    // The BMP writer emits 32-bit BGRA. On a non-32bpp panel (e.g. RGB565) the
    // bytes would be misinterpreted, so skip capture rather than write garbage.
    // fb_bpp is still populated above so meta.txt can report the real depth.
    if (vinfo.bits_per_pixel != BMP_BPP) {
        return false;
    }

    fb_row_bytes = (size_t)w * BMP_BYTES_PER_PIXEL;
    fb_stride    = finfo.line_length ? (size_t)finfo.line_length : fb_row_bytes;
    if (fb_stride < fb_row_bytes) {
        return false;   // implausible; refuse rather than read past each row
    }

    fb_width  = w;
    fb_height = h;
    build_header(w, h);
    initialized = 1;
    return true;
}

// async-signal-safe parser: read non-negative int starting at p, return value.
static int parse_uint(const char* p, const char* end) {
    int v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        ++p;
    }
    return v;
}

// Read the pan_y component of /sys/class/graphics/fb0/pan ("x,y").
// Returns 0 on error (which is a safe default — captures the first page).
static int read_pan_y(void) {
    int fd = open("/sys/class/graphics/fb0/pan", O_RDONLY);
    if (fd < 0) return 0;
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    // Find the comma; pan_y is the integer that follows.
    const char* comma = NULL;
    for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == ',') { comma = buf + i; break; }
    }
    if (!comma) return 0;
    return parse_uint(comma + 1, buf + n);
}

static void write_all(int fd, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return;
        p += n;
        len -= (size_t)n;
    }
}

int  FbCapture_width(void)     { return fb_width; }
int  FbCapture_height(void)    { return fb_height; }
int  FbCapture_bpp(void)       { return fb_bpp; }   // real panel depth, from FBIOGET_VSCREENINFO
bool FbCapture_isAvailable(void) { return initialized != 0; }

ssize_t FbCapture_writeBmp(int fd) {
    if (!initialized) return 0;

    // Desktop: stream straight out of the borrowed SDL surface. Just header +
    // memory reads + write() — no open/lseek, and no SDL call, so this stays
    // async-signal-safe. Rows are copied one at a time so a padded pitch is
    // skipped exactly as the fbdev path skips line_length padding.
    if (surface_pixels) {
        write_all(fd, bmp_header, BMP_HEADER_SIZE);
        for (int y = 0; y < fb_height; ++y) {
            write_all(fd, surface_pixels + (size_t)y * fb_stride, fb_row_bytes);
        }
        return (ssize_t)BMP_HEADER_SIZE + (ssize_t)fb_height * (ssize_t)fb_row_bytes;
    }

    int pan_y = read_pan_y();

    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd < 0) return 0;

    if (pan_y > 0) {
        // Seek to the start of the currently visible page.
        off_t offset = (off_t)pan_y * (off_t)fb_stride;
        if (lseek(fb_fd, offset, SEEK_SET) == (off_t)-1) {
            close(fb_fd);
            return 0;
        }
    }

    // Header first.
    write_all(fd, bmp_header, BMP_HEADER_SIZE);

    // Stream scanlines. Only the visible fb_row_bytes (w*4) go into the BMP; any
    // per-row stride padding (fb_stride - fb_row_bytes) is skipped, so a padded
    // line_length no longer shears the image.
    size_t pad = fb_stride - fb_row_bytes;
    int rows = 0;
    for (int y = 0; y < fb_height; ++y) {
        ssize_t got = read(fb_fd, scanline, fb_row_bytes);
        if (got <= 0) break;
        if ((size_t)got < fb_row_bytes) {
            // Short read: pad the remainder with zeros so the file size matches
            // the header. The pad buffer lives in BSS so it's already zero.
            memset(scanline + got, 0, fb_row_bytes - (size_t)got);
        }
        write_all(fd, scanline, fb_row_bytes);
        rows++;
        if (pad > 0 && y + 1 < fb_height) {
            // Advance past the row padding to the start of the next row.
            if (lseek(fb_fd, (off_t)pad, SEEK_CUR) == (off_t)-1) break;
        }
    }

    close(fb_fd);

    // The header declares fb_height rows. A file with fewer rows is a truncated
    // BMP that SDL_LoadBMP will reject at next startup — and, since the source is
    // never deleted on a failed conversion, it would be retried on every launch
    // forever. Signal "no usable image" so the caller discards the file instead.
    if (rows < fb_height) return 0;

    return (ssize_t)BMP_HEADER_SIZE + (ssize_t)rows * (ssize_t)fb_row_bytes;
}
