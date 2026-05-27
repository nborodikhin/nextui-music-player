# Spec: Project Overview

## Objective

NextUI Music Player is a native C application for TrimUI handheld gaming devices running the NextUI firmware. It provides local music playback, online radio streaming, podcast management, and YouTube music downloading — all accessible through a gamepad-driven UI with no keyboard/mouse input.

Target users are TrimUI device owners who want media playback beyond what the base firmware provides. The app runs as a "pak" (plugin) distributed via the NextUI Pak Store or manual installation.

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C (gnu99 standard) |
| Display/Input | SDL2, SDL2_image, SDL2_ttf |
| GPU rendering | OpenGL ES 2.0 (GLESv2) |
| Audio decoders | dr_mp3.h, dr_wav.h, dr_flac.h, stb_vorbis.h, minimp4.h (all header-only) |
| Opus support | libopus + libogg + opusfile (source-bundled) |
| AAC (tg5050) | libfdk-aac (prebuilt .so) |
| Resampling | libsamplerate |
| FFT / spectrum | kiss_fft (bundled) |
| HTTPS | mbedTLS (source-bundled) |
| JSON | parson (source-bundled) |
| XML | yxml (source-bundled) |
| ZIP | libzip |
| Platform | NextUI `platform.c` + `api.h` + `msettings` |
| Cross-compile | aarch64-nextui-linux-gnu-gcc |
| Targets | tg5040 (TrimUI Smart Pro / Brick), tg5050 (TrimUI Smart Pro S) |

## Commands

```bash
# Enter the Docker toolchain shell (run from NextUI repo root)
make shell PLATFORM=tg5040

# Inside toolchain shell — build for tg5040 (TrimUI Brick)
cd ~/workspace/nextui-music-player/src
make clean && make PLATFORM=tg5040

# Build for tg5050 (TrimUI Smart Pro S)
make clean && make PLATFORM=tg5050

# Build and push to device via ADB (run from project root on host)
sh build.sh

# Build on device only (run inside Docker from project root)
sh build-device.sh

# Clean build artifacts
cd src && make clean
```

CI builds via GitHub Actions (`release.yml`) use
`ghcr.io/loveretro/tg5040-toolchain:latest` and
`ghcr.io/loveretro/tg5050-toolchain:latest` Docker images.

## Project Structure

```
nextui-music-player/
├── src/                    # All C source code
│   ├── musicplayer.c       # main() entry point — init, main loop, cleanup
│   ├── Makefile            # Cross-compilation rules
│   │
│   ├── player.{c,h}        # Core audio engine (PCM decode, SDL audio callback)
│   ├── playlist.{c,h}      # Playlist management (directory scan, M3U)
│   ├── playlist_m3u.{c,h}  # M3U file parser/writer
│   ├── spectrum.{c,h}      # FFT-based spectrum visualizer (GPU)
│   ├── album_art.{c,h}     # Embedded + remote album art fetching
│   ├── lyrics.{c,h}        # Lyrics fetching and sync
│   ├── resume.{c,h}        # Playback resume state (persist to disk)
│   ├── background.{c,h}    # Background playback manager
│   ├── settings.{c,h}      # App-specific settings (screen timeout, bass, etc.)
│   │
│   ├── radio.{c,h}         # Radio streaming core (ICY/Icecast/Shoutcast)
│   ├── radio_net.{c,h}     # HTTP radio network layer
│   ├── radio_hls.{c,h}     # HLS (m3u8) streaming support
│   ├── radio_curated.{c,h} # Bundled curated station lists (by country)
│   │
│   ├── podcast.{c,h}       # Podcast subscription, episode mgmt, download queue
│   ├── podcast_rss.c       # RSS XML parser (uses yxml)
│   ├── podcast_search.c    # iTunes Search API + Apple Charts
│   │
│   ├── downloader.{c,h}    # YouTube Music downloader (yt-dlp wrapper)
│   ├── http_download.{c,h} # Generic HTTP file downloader (mbedTLS-based)
│   ├── wget_fetch.{c,h}    # Thin wrapper: wget binary for simple fetches
│   ├── wifi.{c,h}          # WiFi status helpers
│   ├── selfupdate.{c,h}    # In-app self-update (GitHub Releases)
│   ├── keyboard.{c,h}      # On-screen keyboard launcher
│   │
│   ├── module_common.{c,h} # Global input, screen-off, toast, volume
│   ├── module_menu.{c,h}   # Main menu screen
│   ├── module_library.{c,h}# File browser / library screen
│   ├── module_player.{c,h} # Music playback screen
│   ├── module_radio.{c,h}  # Radio playback screen
│   ├── module_podcast.{c,h}# Podcast browsing/playback screen
│   ├── module_downloader.{c,h} # YouTube downloader screen
│   ├── module_system.{c,h} # System info / About screen
│   ├── module_settings.{c,h}  # Settings screen
│   │
│   ├── ui_main.{c,h}       # Menu/dialog rendering
│   ├── ui_music.{c,h}      # Music player UI
│   ├── ui_radio.{c,h}      # Radio player UI
│   ├── ui_podcast.{c,h}    # Podcast UI
│   ├── ui_playlist.{c,h}   # Playlist editor UI
│   ├── ui_downloader.{c,h} # Downloader UI
│   ├── ui_album_art.{c,h}  # Album art display helpers
│   ├── ui_settings.{c,h}   # Settings UI
│   ├── ui_system.{c,h}     # System/About UI
│   ├── ui_fonts.{c,h}      # Font loading (SDL_ttf)
│   ├── ui_icons.{c,h}      # Icon loading (PNG per file type)
│   ├── ui_utils.{c,h}      # Scrolling text, list rendering helpers
│   ├── browser.{c,h}       # Generic file browser widget
│   ├── display_helper.{c,h}# TG5050 DRM display recovery
│   │
│   ├── add_to_playlist.{c,h} # "Add to playlist" dialog
│   │
│   ├── audio/              # Header-only audio decoders
│   │   ├── dr_mp3.h        # MP3 decoder
│   │   ├── dr_wav.h        # WAV decoder
│   │   ├── dr_flac.h       # FLAC decoder
│   │   ├── stb_vorbis.h    # OGG Vorbis decoder
│   │   ├── minimp4.h       # M4A/AAC container
│   │   └── kiss_fft*.{c,h} # FFT implementation
│   │
│   └── include/            # Bundled third-party libraries
│       ├── mbedtls_lib/    # mbedTLS (TLS/HTTPS)
│       ├── libogg/         # OGG container
│       ├── libopus/        # Opus codec
│       ├── opusfile/       # Opus file access
│       ├── parson/         # JSON parser
│       └── yxml/           # XML parser
│
├── bin/                    # Runtime binaries and build output
│   ├── tg5040/             # tg5040 build output (musicplayer.elf)
│   ├── tg5050/             # tg5050 build output + libfdk-aac.so
│   ├── yt-dlp              # YouTube downloader binary (ARM64)
│   ├── wget                # HTTP downloader binary (ARM64)
│   └── keyboard            # On-screen keyboard binary (ARM64)
│
├── res/                    # App resources
│   ├── font.ttf            # UI font
│   └── icon-*.png          # File type icons (mp3, flac, wav, etc.)
│
├── stations/               # Curated radio station JSON files (by country)
│   ├── MLA.json            # Malaysia
│   ├── SNG.json            # Singapore
│   └── USA.json            # United States
│
├── state/                  # Runtime state (persisted on device SD card)
│   ├── app_version.txt     # Current app version (read by self-update)
│   └── yt-dlp_version.txt  # yt-dlp version
│
├── spec/                   # Spec-driven development documentation
├── pak.json                # Pak metadata (name, version, platforms)
├── launch.sh               # NextUI pak entry point
├── build.sh                # Build + ADB push to device
├── build-device.sh         # Build inside Docker toolchain
└── .github/workflows/
    └── release.yml         # CI: Docker cross-compile + GitHub Release
```

## Success Criteria (ongoing)

- Builds cleanly for both tg5040 and tg5050 via `make PLATFORM=<target>`
- All audio formats (WAV, MP3, OGG, FLAC, M4A, AAC, Opus) play correctly
- Radio, podcast, and YouTube downloader work over WiFi
- Background playback continues when user returns to main menu
- Resume state survives app restart
- No regressions detected when a new feature is added
