#ifndef __SPECTRUM_H__
#define __SPECTRUM_H__

#include <stdbool.h>

#define SPECTRUM_FFT_SIZE 512
#define SPECTRUM_BARS 64

typedef enum {
    SPECTRUM_STYLE_VERTICAL = 0, // Vertical gradient within each bar (default)
    SPECTRUM_STYLE_SOLID,        // One bar color, from the theme
    SPECTRUM_STYLE_RAINBOW,      // Rainbow gradient across bars
    SPECTRUM_STYLE_MAGNITUDE,    // Green (low) to red (high) like VU meter
    SPECTRUM_STYLE_COUNT
} SpectrumStyle;

void Spectrum_init(void);
void Spectrum_quit(void);
void Spectrum_update(void);

void Spectrum_setPosition(int x, int y, int w, int h);
bool Spectrum_needsRefresh(void);

// The spectrum, as painted onto the player's overlay layer.
bool Spectrum_isShowing(void);
void Spectrum_paint(int layer);

// Rotate through each style and then off. The only control of the spectrum.
void Spectrum_cycleNext(void);

#endif
