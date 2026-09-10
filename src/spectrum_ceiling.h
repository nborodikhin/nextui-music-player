#ifndef __SPECTRUM_CEILING_H__
#define __SPECTRUM_CEILING_H__

#include <stdbool.h>
#include <stdint.h>

// The ceiling of the spectrum. It limits each spectrum bar while the spectrum
// comes back, thus the bars inflate into the shape of the sound in place of one
// frame.
//
// The ceiling has no fall of its own. Where the sound stops, each spectrum bar
// falls at the rate that it has while the sound plays, and the owner clears the
// ceiling when nothing is left. Thus the next sound inflates again.
//
// The rise is of the clock and not of the frame. A screen that stops drawing - a
// dialog over it, a screen that goes off - therefore comes back to the value that
// the time gives, and not to the value that it left.
//
// Pure float logic, no platform includes.
typedef struct {
    float    value;
    uint32_t last_ms;
    bool     started;
} SpectrumCeiling;

// No spectrum. The next sound inflates from nothing.
void SpectrumCeiling_clear(SpectrumCeiling* ceiling);

// Take the ceiling to the time `now_ms`. `rising` is true where a whole frame of
// samples reached the spectrum since the last call. Where it is false the value
// holds, because the bars carry the fall.
void SpectrumCeiling_step(SpectrumCeiling* ceiling, bool rising, uint32_t now_ms);

// None to full. None means that the spectrum draws nothing.
float SpectrumCeiling_value(const SpectrumCeiling* ceiling);

// True while the spectrum has a height to draw.
bool SpectrumCeiling_isOpen(const SpectrumCeiling* ceiling);

#endif
