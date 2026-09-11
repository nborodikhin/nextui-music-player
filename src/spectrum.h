#ifndef __SPECTRUM_H__
#define __SPECTRUM_H__

#include <stdbool.h>
#include <stdint.h>

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

// Take the bars and the ceiling to nothing, and keep the position of the box.
// A source that starts - a station that the user selects - calls this, thus the
// sound that went does not fall on the screen of the sound that comes.
void Spectrum_reset(void);

void Spectrum_setPosition(int x, int y, int w, int h);
bool Spectrum_needsRefresh(void);

// The spectrum, as painted onto the player's overlay layer.
bool Spectrum_isShowing(void);
void Spectrum_paint(int layer);

// Rotate through each style and then off. The only control of the spectrum.
void Spectrum_cycleNext(void);

// --- Internal: spectrum_ceiling.c ---

// The ceiling logic of the spectrum.
//
// It is used to implement opening animation, where spectrogram gradually rises
// (on the bar level) over a short period of time.
//
// The ceiling has no fall of its own - on sound stop spectrum bars fall
// at their natural fall rate, and the owner clears the ceiling after that.

typedef struct {
    float    value;
    uint32_t last_ms;
    bool     started;
} SpectrumCeiling;

// Reset the ceiling animation.
void SpectrumCeiling_clear(SpectrumCeiling* ceiling);

// Advance the ceiling animation to the time `now_ms`.
// Pass `sound_is_playing` if the period from the last tick should be considered
// as part of rising flow (for example buffering time should not).
void SpectrumCeiling_tick(SpectrumCeiling* ceiling, bool sound_is_playing, uint32_t now_ms);

// Current value of the ceiling, 0..1. At 0, spectrogram should not be drawn.
float SpectrumCeiling_value(const SpectrumCeiling* ceiling);

#endif
