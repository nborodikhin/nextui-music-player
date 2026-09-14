#include "spectrum.h"
#include "ui_layers.h"
#include "ui_theme.h"
#include "player.h"
#include "file_utils.h"
#include "defines.h"
#include "api.h"
#include "audio/kiss_fftr.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define OLD_SPECTRUM_SETTINGS_FILE SHARED_USERDATA_PATH "/spectrum_settings.txt"

#define SMOOTHING_FACTOR 0.7f

// The fall of a bar where no sound feeds it. It is the rate that SMOOTHING_FACTOR
// gives while the sound plays - 0.7 of the height each frame of 60 - expressed as
// a time, thus a slow loop and a screen that stops drawing both give the same
// fall. A bar of the whole height reaches BAR_MIN in about 0.2 seconds.
#define BAR_FALL_TAU_MS 47.0f

// Below this a bar has no height to draw.
#define BAR_MIN 0.01f

// The loop draws more often than the audio callback fills the buffer, thus a
// frame with no new samples is normal and says nothing. The sound has stopped
// only where no frame of samples arrives for this long.
//
// The device takes 2048 frames each callback (`AUDIO_SAMPLES` of `player.c`),
// which is about 46 ms at 44100. A read must also give a whole frame of the
// window, thus a callback that runs short gives nothing at all. This holds five
// callbacks, and a fall that starts after them is still quick to the eye.
#define SOUND_GONE_MS 250u
#define PEAK_DECAY 0.97f
#define MIN_DB -60.0f
#define MAX_DB 0.0f
#define FREQ_COMPENSATION 1.0f  // dB boost per octave for high frequencies
#define FREQ_DISTRIBUTION 0.6f  // <1.0 = more bars for high freq, >1.0 = more bars for low freq

static kiss_fftr_cfg fft_cfg = NULL;
static kiss_fft_scalar fft_input[SPECTRUM_FFT_SIZE];
static kiss_fft_cpx fft_output[SPECTRUM_FFT_SIZE / 2 + 1];
static float hann_window[SPECTRUM_FFT_SIZE];
static float prev_bars[SPECTRUM_BARS];
typedef struct {
    float bars[SPECTRUM_BARS];
    float peaks[SPECTRUM_BARS];
    bool valid;
} SpectrumData;

static SpectrumData spectrum_data;
static int16_t sample_buffer[SPECTRUM_FFT_SIZE * 2];

static int bin_ranges[SPECTRUM_BARS + 1];
static float freq_compensation[SPECTRUM_BARS];  // Per-band gain compensation

static int spec_x = 0, spec_y = 0, spec_w = 0, spec_h = 0;
static bool position_set = false;

static SpectrumStyle current_style = SPECTRUM_STYLE_VERTICAL;
static bool spectrum_visible = true;

static SpectrumCeiling ceiling;

// The clock of the last fall of the bars. The fall is of the time and not of the
// frame, thus a screen that stops drawing comes back to the height that the time
// gives.
static uint32_t bars_fall_last_ms = 0;

// The clock of the last whole frame of samples. It says whether the sound is
// still there between two callbacks of the audio.
static uint32_t last_samples_ms = 0;

// Returns true after it replaces the spectrum settings file with a complete file.
static bool save_settings(void) {
    if (!userdata_mkdir("")) return false;

    char path[512];
    int length = userdata_snpath("spectrum_settings.txt", path, sizeof(path));
    if (length < 0 || (size_t)length >= sizeof(path)) return false;

    char temp_path[sizeof(path) + sizeof(".tmp")];
    length = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (length < 0 || (size_t)length >= sizeof(temp_path)) return false;

    FILE* f = fopen(temp_path, "w");
    if (!f) return false;

    bool saved = fprintf(f, "%d\n%d\n", (int)current_style,
                         spectrum_visible ? 1 : 0) >= 0;
    if (saved && fflush(f) != 0) saved = false;
    if (saved && fsync(fileno(f)) != 0) saved = false;
    if (fclose(f) != 0) saved = false;

    if (saved && rename(temp_path, path) == 0) return true;

    remove(temp_path);
    return false;
}

void Spectrum_initSettings(void) {
    char path[512];
    int length = userdata_snpath("spectrum_settings.txt", path, sizeof(path));
    if (length < 0 || (size_t)length >= sizeof(path)) return;

    bool loaded_old = false;
    FILE* f = fopen(path, "r");
    if (!f) {
        f = fopen(OLD_SPECTRUM_SETTINGS_FILE, "r");
        loaded_old = f != NULL;
    }
    if (!f) return;

    int style = 0, visible = 1;
    bool loaded = fscanf(f, "%d\n%d\n", &style, &visible) == 2;
    if (loaded) {
        if (style >= 0 && style < SPECTRUM_STYLE_COUNT) {
            current_style = (SpectrumStyle)style;
        }
        spectrum_visible = (visible != 0);
    }
    fclose(f);

    if (loaded_old && loaded && save_settings()) {
        remove(OLD_SPECTRUM_SETTINGS_FILE);
    }
}

// HSV to RGB conversion (h: 0-360, s: 0-1, v: 0-1)
static void hsv_to_rgb(float h, float s, float v, uint8_t* r, uint8_t* g, uint8_t* b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;

    if (h < 60)      { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }

    *r = (uint8_t)((rf + m) * 255);
    *g = (uint8_t)((gf + m) * 255);
    *b = (uint8_t)((bf + m) * 255);
}

// The distance between the strongest and the weakest channel of a color.
// A gray has a chroma of 0.
static int chroma(SDL_Color color) {
    uint8_t high = color.r > color.g ? color.r : color.g;
    if (color.b > high) high = color.b;
    uint8_t low = color.r < color.g ? color.r : color.g;
    if (color.b < low) low = color.b;
    return high - low;
}

// The top color of the gradient: the color of the theme that has the most hue.
// Most themes keep the two accents as two shades of the background, thus a
// gradient between them is flat. Their hue is in COLOR_MAIN. The default theme
// is the other way round, because its COLOR_MAIN is white.
static SDL_Color gradient_top(void) {
    SDL_Color accent = uintToColour(CFG_getColor(COLOR_ACCENT));
    SDL_Color main_color = uintToColour(CFG_getColor(COLOR_MAIN));
    return chroma(accent) > chroma(main_color) ? accent : main_color;
}

// Get color for a bar based on current style
static void get_bar_color(int bar_index, float magnitude, uint8_t* r, uint8_t* g, uint8_t* b) {
    float t;
    switch (current_style) {
        case SPECTRUM_STYLE_RAINBOW:
            // Rainbow: red -> orange -> yellow -> green -> cyan -> blue -> purple
            t = (float)bar_index / (SPECTRUM_BARS - 1);
            hsv_to_rgb(t * 270.0f, 1.0f, 1.0f, r, g, b);  // 0-270 hue range
            break;

        case SPECTRUM_STYLE_MAGNITUDE:
            // VU meter style: green (low) -> yellow -> red (high)
            if (magnitude < 0.5f) {
                // Green to yellow
                *r = (uint8_t)(magnitude * 2 * 255);
                *g = 255;
                *b = 0;
            } else {
                // Yellow to red
                *r = 255;
                *g = (uint8_t)((1.0f - (magnitude - 0.5f) * 2) * 255);
                *b = 0;
            }
            break;

        case SPECTRUM_STYLE_VERTICAL: {
            // The top of the gradient. A bar that reached this level had that
            // color at its top, thus the peak mark that falls from it keeps it.
            SDL_Color peak = gradient_top();
            *r = peak.r;
            *g = peak.g;
            *b = peak.b;
            break;
        }

        case SPECTRUM_STYLE_SOLID:
        default: {
            // The color that the theme gives to a title on the page. The role
            // layer holds it legible on the background of every theme, thus a
            // light theme gets a dark bar and not a white one.
            SDL_Color solid = Theme_getColor(THEME_ROLE_PRIMARY, false);
            *r = solid.r;
            *g = solid.g;
            *b = solid.b;
            break;
        }
    }
}

static void init_hann_window(void) {
    for (int i = 0; i < SPECTRUM_FFT_SIZE; i++) {
        hann_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (SPECTRUM_FFT_SIZE - 1)));
    }
}

static void init_bin_ranges(void) {
    float min_freq = 80.0f;
    float max_freq = 16000.0f;
    float sample_rate = 48000.0f;
    float bin_resolution = sample_rate / SPECTRUM_FFT_SIZE;

    int min_bin = (int)(min_freq / bin_resolution);
    int max_bin = (int)(max_freq / bin_resolution);
    if (max_bin > SPECTRUM_FFT_SIZE / 2) max_bin = SPECTRUM_FFT_SIZE / 2;

    for (int i = 0; i <= SPECTRUM_BARS; i++) {
        float t = (float)i / SPECTRUM_BARS;
        // Apply distribution curve: <1.0 compresses low freq, expands high freq
        t = powf(t, FREQ_DISTRIBUTION);
        float freq = min_freq * powf(max_freq / min_freq, t);
        int bin = (int)(freq / bin_resolution);
        if (bin < min_bin) bin = min_bin;
        if (bin > max_bin) bin = max_bin;
        bin_ranges[i] = bin;
    }

    // Initialize frequency compensation (boost higher frequencies)
    // This compensates for the natural 1/f energy distribution in audio
    for (int i = 0; i < SPECTRUM_BARS; i++) {
        float t = (float)i / (SPECTRUM_BARS - 1);
        // Apply progressive boost: 0 dB at lowest, FREQ_COMPENSATION*octaves at highest
        float octaves = log2f(max_freq / min_freq);
        freq_compensation[i] = t * octaves * FREQ_COMPENSATION;
    }
}

void Spectrum_init(void) {
    // The bars rise from nothing on each visit
    SpectrumCeiling_clear(&ceiling);
    last_samples_ms = 0;
    bars_fall_last_ms = 0;

    // The position stays as the render of the screen set it. `Spectrum_quit()`
    // clears it on the way out, thus a stale position cannot reach the next
    // screen, and a screen that renders one time keeps the position that it gave.

    if (fft_cfg) return;  // Already initialized

    fft_cfg = kiss_fftr_alloc(SPECTRUM_FFT_SIZE, 0, NULL, NULL);
    init_hann_window();
    init_bin_ranges();
    memset(prev_bars, 0, sizeof(prev_bars));
    memset(&spectrum_data, 0, sizeof(spectrum_data));
}

void Spectrum_quit(void) {
    // The layer must wait for the position of the next visit
    position_set = false;

    // The way out of a playing screen is the one exit with no fall
    SpectrumCeiling_clear(&ceiling);

    if (fft_cfg) {
        kiss_fftr_free(fft_cfg);
        fft_cfg = NULL;
    }
}

static bool bars_have_height(void) {
    for (int i = 0; i < SPECTRUM_BARS; i++) {
        if (prev_bars[i] > BAR_MIN) return true;
    }
    return false;
}

// Take each bar down to the height that the elapsed time gives, and take the peak
// markers away.
static void fall_bars(uint32_t now_ms) {
    uint32_t dt = now_ms - bars_fall_last_ms;
    bars_fall_last_ms = now_ms;
    if (dt == 0) return;
    if (dt > 60000u) dt = 60000u;

    float factor = expf(-(float)dt / BAR_FALL_TAU_MS);

    for (int i = 0; i < SPECTRUM_BARS; i++) {
        prev_bars[i] *= factor;
        if (prev_bars[i] < BAR_MIN) prev_bars[i] = 0.0f;
        spectrum_data.bars[i] = prev_bars[i];
        spectrum_data.peaks[i] = 0.0f;
    }
}

void Spectrum_reset(void) {
    SpectrumCeiling_clear(&ceiling);
    last_samples_ms = 0;
    bars_fall_last_ms = 0;

    memset(prev_bars, 0, sizeof(prev_bars));
    memset(&spectrum_data, 0, sizeof(spectrum_data));

    // The buffer can hold what the sound that went wrote into it. A read takes
    // those samples away, thus they do not read as the sound of the new source.
    Player_getVisBuffer(sample_buffer, SPECTRUM_FFT_SIZE * 2);
}

void Spectrum_update(void) {
    if (!fft_cfg) return;

    // A read takes the samples, thus a read that gives less than a whole frame
    // says that the sound stopped. The reason does not matter: a pause, a stop,
    // an empty buffer or the end of a track each give the same answer.
    //
    // The window reads two values for each sample of the frame, thus a whole
    // frame is SPECTRUM_FFT_SIZE * 2 values. A read that gives less holds the
    // values of the last frame in its upper part, thus it is not a frame.
    int samples = Player_getVisBuffer(sample_buffer, SPECTRUM_FFT_SIZE * 2);
    bool samples_arrived = samples >= SPECTRUM_FFT_SIZE * 2;

    uint32_t now = SDL_GetTicks();

    if (samples_arrived) last_samples_ms = now;

    // The control that turns the spectrum off says the same as a sound that
    // stops, and it says it at once.
    bool sound_is_playing = (now - last_samples_ms) < SOUND_GONE_MS;
    bool feeding = samples_arrived && spectrum_visible;

    // Drive the rising of the spectrum during opening
    SpectrumCeiling_tick(&ceiling, spectrum_visible && sound_is_playing, now);

    if (!samples_arrived && spectrum_visible && sound_is_playing) {
        // A frame between two callbacks of the audio. The bars keep the height
        // that the last frame of samples gave them.
        bars_fall_last_ms = now;
        return;
    }

    if (!feeding) {
        // Each bar falls at its own rate, as it does in a quiet passage of the
        // sound. The peak markers go at once: a mark of a peak that no bar can
        // reach again reads as a fault of the screen.
        fall_bars(now);
        spectrum_data.valid = bars_have_height();

        // Nothing is left, thus the next sound inflates from nothing again.
        if (!spectrum_data.valid) SpectrumCeiling_clear(&ceiling);
        return;
    }

    bars_fall_last_ms = now;

    for (int i = 0; i < SPECTRUM_FFT_SIZE; i++) {
        float left = sample_buffer[i * 2];
        float right = sample_buffer[i * 2 + 1];
        float mono = (left + right) * 0.5f;
        fft_input[i] = (mono / 32768.0f) * hann_window[i];
    }

    kiss_fftr(fft_cfg, fft_input, fft_output);

    for (int i = 0; i < SPECTRUM_BARS; i++) {
        int start_bin = bin_ranges[i];
        int end_bin = bin_ranges[i + 1];
        if (end_bin <= start_bin) end_bin = start_bin + 1;

        float sum = 0.0f;
        int count = 0;
        for (int j = start_bin; j < end_bin && j < SPECTRUM_FFT_SIZE / 2 + 1; j++) {
            float re = fft_output[j].r;
            float im = fft_output[j].i;
            float mag = sqrtf(re * re + im * im);
            sum += mag;
            count++;
        }

        float avg_mag = (count > 0) ? sum / count : 0.0f;

        float db = 20.0f * log10f(avg_mag + 1e-10f);
        // Apply frequency compensation to boost higher frequencies
        db += freq_compensation[i];
        float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;

        if (normalized > prev_bars[i]) {
            prev_bars[i] = normalized;
        } else {
            prev_bars[i] = prev_bars[i] * SMOOTHING_FACTOR + normalized * (1.0f - SMOOTHING_FACTOR);
        }

        spectrum_data.bars[i] = prev_bars[i];

        if (prev_bars[i] > spectrum_data.peaks[i]) {
            spectrum_data.peaks[i] = prev_bars[i];
        } else {
            spectrum_data.peaks[i] *= PEAK_DECAY;
        }
    }

    spectrum_data.valid = true;
}

void Spectrum_setPosition(int x, int y, int w, int h) {
    spec_x = x;
    spec_y = y;
    spec_w = w;
    spec_h = h;
    position_set = true;
}

bool Spectrum_needsRefresh(void) {
    if (!position_set) return false;

    // A style that is on must read the samples on each frame, otherwise the
    // spectrum cannot see a sound that starts again. A style that is off still
    // carries the fall to its end. Neither says that the layer must paint: ask
    // `Spectrum_isShowing()` for that.
    return spectrum_visible || (SpectrumCeiling_value(&ceiling) > 0.0f);
}

void Spectrum_cycleNext(void) {
    if (!spectrum_visible) {
        // Off -> first style
        spectrum_visible = true;
        current_style = SPECTRUM_STYLE_VERTICAL;
    } else {
        int next = (int)current_style + 1;
        if (next >= SPECTRUM_STYLE_COUNT) {
            // Last style -> off
            spectrum_visible = false;
        } else {
            current_style = (SpectrumStyle)next;
        }
    }
    save_settings();
}

static void interpolate_gradient(SDL_Color top, SDL_Color bottom, float t,
        uint8_t* r, uint8_t* g, uint8_t* b) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    *r = (uint8_t)(top.r + t * (bottom.r - top.r));
    *g = (uint8_t)(top.g + t * (bottom.g - top.g));
    *b = (uint8_t)(top.b + t * (bottom.b - top.b));
}

// Draw a vertical gradient bar (for SPECTRUM_STYLE_VERTICAL).
// The gradient is fixed to the spectrum height so equal heights have equal
// colors across every bar.
static void draw_vertical_gradient_bar(SDL_Surface* surface, int x, int y,
        int w, int h, int gradient_y, int gradient_height) {
    if (h <= 0 || w <= 0) return;

    // uintToColour() reads the packed form that CFG_getColor() gives. Do not
    // take the channels by hand: the platform added an alpha byte to that form
    // and every shift moved by eight bits.
    SDL_Color top = gradient_top();
    SDL_Color bottom = uintToColour(CFG_getColor(COLOR_ACCENT2));

    for (int row = 0; row < h; row++) {
        float t = (float)(y + row - gradient_y) / (float)(gradient_height - 1);
        uint8_t r, g, b;
        interpolate_gradient(top, bottom, t, &r, &g, &b);

        // Use SDL_MapRGBA for correct pixel format
        uint32_t color = SDL_MapRGBA(surface->format, r, g, b, 255);

        SDL_Rect row_rect = {x, y + row, w, 1};
        SDL_FillRect(surface, &row_rect, color);
    }
}

bool Spectrum_isShowing(void) {
    return position_set && (SpectrumCeiling_value(&ceiling) > 0.0f) &&
            spectrum_data.valid;
}

void Spectrum_paint(UiLayer layer) {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0,
        spec_w, spec_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;

    SDL_FillRect(surface, NULL, 0);

    int total_bars = SPECTRUM_BARS;
    float bar_width_f = (float)spec_w / total_bars;
    int bar_gap = 1;
    int bar_draw_w = (int)bar_width_f - bar_gap;
    if (bar_draw_w < 1) bar_draw_w = 1;
    int gradient_height = (int)(spec_h * 0.9f);
    if (gradient_height < 2) gradient_height = 2;
    int gradient_y = spec_h - gradient_height;

    // The ceiling is a lid on each bar and each peak marker. A bar under it keeps
    // its own height, thus the shape of the sound stays while the lid moves.
    float lid = SpectrumCeiling_value(&ceiling);

    // The base line of a bar goes with the lid on the way up, thus the block that
    // inflates has one edge. A bar that has fallen to nothing keeps no base line,
    // otherwise the fall ends with a row of marks that goes in one frame.
    int base_h = (int)(lid * 2.0f + 0.5f);

    for (int i = 0; i < total_bars; i++) {
        float magnitude = fminf(spectrum_data.bars[i], lid);
        int bar_h = (int)(magnitude * spec_h * 0.9f);
        if (magnitude > BAR_MIN && bar_h < base_h) bar_h = base_h;

        int bar_x_pos = (int)(i * bar_width_f);
        int bar_y_pos = spec_h - bar_h;

        if (current_style == SPECTRUM_STYLE_VERTICAL) {
            // Vertical gradient - draw pixel by pixel
            draw_vertical_gradient_bar(surface, bar_x_pos, bar_y_pos,
                bar_draw_w, bar_h, gradient_y, gradient_height);
        } else {
            // Solid color styles
            uint8_t r, g, b;
            get_bar_color(i, magnitude, &r, &g, &b);
            uint32_t color = SDL_MapRGBA(surface->format, r, g, b, 255);

            SDL_Rect bar_rect = {bar_x_pos, bar_y_pos, bar_draw_w, bar_h};
            SDL_FillRect(surface, &bar_rect, color);
        }

        // Draw peak indicator
        float peak = fminf(spectrum_data.peaks[i], lid);
        if (peak > magnitude + 0.02f) {
            int peak_y = spec_h - (int)(peak * spec_h * 0.9f);
            uint8_t r, g, b;
            if (current_style == SPECTRUM_STYLE_VERTICAL) {
                SDL_Color top = gradient_top();
                SDL_Color bottom = uintToColour(CFG_getColor(COLOR_ACCENT2));
                float t = (float)(peak_y - gradient_y) / (float)(gradient_height - 1);
                interpolate_gradient(top, bottom, t, &r, &g, &b);
            } else {
                get_bar_color(i, peak, &r, &g, &b);
            }
            uint32_t peak_color = SDL_MapRGBA(surface->format, r, g, b, 255);
            SDL_Rect peak_rect = {bar_x_pos, peak_y, bar_draw_w, 2};
            SDL_FillRect(surface, &peak_rect, peak_color);
        }
    }

    UiLayer_blit(surface, spec_x, spec_y, layer);
    SDL_FreeSurface(surface);
}
