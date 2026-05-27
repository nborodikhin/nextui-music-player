# Spec: Audio Engine

## Supported Formats

| Format | Decoder | Notes |
|--------|---------|-------|
| MP3 | `dr_mp3.h` | Header-only, no external dep |
| WAV | `dr_wav.h` | Header-only |
| FLAC | `dr_flac.h` | Header-only |
| OGG Vorbis | `stb_vorbis.h` | Header-only |
| M4A / AAC | `minimp4.h` + platform | tg5050 uses libfdk-aac.so |
| Opus | libopus + libogg + opusfile | Compiled separately (OPUS_CFLAGS isolation) |

Format detection via `Player_detectFormat(filepath)` — extension-based with file magic fallback.

## Player Core (`player.{c,h}`)

### State Machine

```
PLAYER_STATE_STOPPED → Player_load() → PLAYER_STATE_STOPPED (file loaded)
                     → Player_play() → PLAYER_STATE_PLAYING
PLAYER_STATE_PLAYING → Player_pause() → PLAYER_STATE_PAUSED
                     → Player_stop() → PLAYER_STATE_STOPPED
PLAYER_STATE_PAUSED  → Player_play() → PLAYER_STATE_PLAYING
```

### Streaming Architecture

All formats use a **streaming decode pipeline**:

```
StreamDecoder (format-specific decoder)
    │ decodes N frames
    ▼
libsamplerate (SRC_STATE*)
    │ resamples to 44100 Hz stereo
    ▼
CircularBuffer (~3 seconds at 44.1kHz, ~500KB)
    │ ring buffer protected by pthread_mutex
    ▼
SDL audio callback → DAC
```

- `stream_thread` runs the decode+resample loop continuously
- `CircularBuffer` decouples decode rate from playback rate
- Seeking sets `stream_seeking = true` + `seek_target_frame`; stream thread drains and repositions decoder

### Visualization Buffer

`Player_getVisBuffer()` copies the last 2048 stereo samples into `vis_buffer` (protected by `vis_mutex`). The spectrum module reads this on the main thread and runs FFT there.

### Volume

Software volume is a float multiplier applied to the PCM samples in the audio callback (0.0–1.0). Hardware volume is set via NextUI `SetVolume()` API. On Bluetooth/USB DAC connections the software volume is set to a cubic-curve mapping of the hardware setting; on speaker output it is set to 1.0 (hardware controls everything).

### Playback Speed

`Player_setPlaybackSpeed(float)` — 0.5×–2.0×. Implemented by changing the libsamplerate conversion ratio. Does not affect pitch (no pitch correction; device is not powerful enough for WSOLA).

## Waveform Overview (`WaveformData`)

A static 128-bar amplitude overview of the full file is computed once after load. Used as a progress bar background in the music player UI. Stored in `PlayerContext.waveform`.

## Album Art (`album_art.{c,h}`)

1. Try to extract embedded cover art from the file's tags (ID3v2 APIC, FLAC PICTURE, etc.)
2. If not found: query iTunes Search API by track title + artist, download JPEG thumbnail
3. Result is an `SDL_Surface*` stored in `PlayerContext.album_art`

## Lyrics (`lyrics.{c,h}`)

- Fetches `.lrc` files from an online lyrics API (LRCLIB or similar)
- Parsed into timed lines
- Module polls `Player_getPosition()` to highlight the current line during playback
- Enabled/disabled via `Settings_getLyricsEnabled()`

## Spectrum Visualizer (`spectrum.{c,h}`)

- **FFT size**: 512 samples (kiss_fft)
- **Output**: 64 bars with peak-hold per bar
- **GPU layer**: `LAYER_SPECTRUM = 5` (OpenGL ES quad strips)
- **Styles**: Vertical gradient / White / Rainbow / Magnitude (green→red)
- Cycles through 4 styles then "off" via `Spectrum_cycleNext()`

## Radio Streaming (`radio.{c,h}`, `radio_net.{c,h}`, `radio_hls.{c,h}`)

### Architecture

```
Radio_play(url)
    │
    ├── Detect stream type (HLS m3u8 vs direct)
    ├── Connect via radio_net (HTTP, HTTPS via mbedTLS)
    ├── ICY metadata parsing (Shoutcast/Icecast headers + inline metadata)
    └── Pump audio bytes into 64KB RADIO_BUFFER_SIZE ring buffer
```

- Supports `MP3` and `AAC` streams
- `Radio_getAudioSamples()` is called by SDL audio callback on main thread
- HLS handled in `radio_hls.c`: playlist fetch → segment download → audio decode
- Album art for the current song fetched from iTunes based on ICY title metadata

### Station Storage

- **User stations** (editable): `.userdata/shared/music-player/radio/stations.txt`
- **Curated stations**: read-only JSON files in `stations/` bundled with the pak

## Podcast Playback (`podcast.{c,h}`)

Podcast episodes are MP3/M4A/Opus files downloaded to local storage. Playback goes through the same `Player_*` API as local music. Progress (resume position) is stored in `progress.json` per episode GUID.

`Podcast_loadAndSeek()` pre-positions the player without starting; caller polls `Player_resume()` until seek completes, then calls `Player_play()`.

## USB HID (`Player_initUSBHID`, `Player_pollUSBHID`)

Monitors `/dev/input/eventN` for USB HID media control events (volume up/down, next/prev track, play/pause) from USB earphones or Bluetooth HID devices. Polled on the main thread via `Player_pollUSBHID()`.
