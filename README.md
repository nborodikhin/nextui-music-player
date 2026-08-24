# NextUI Music Player
A comprehensive music playback application for NextUI featuring local file playback, online radio streaming, podcast and music downloading.

> **Note:**
>
> This is a fork of [mohammadsyuhada/nextui-music-player](https://github.com/mohammadsyuhada/nextui-music-player)<br>
> [Support](https://ko-fi.com/Y8Y61SI04B) the original author on ko-fi.

## Supported Platforms
- **tg5040** - TrimUI Smart Pro / TrimUI Brick / Brick Hammer
- **tg5050** - TrimUI Smart Pro S

![music_player](https://github.com/user-attachments/assets/de4fe612-1c48-4e98-9537-79504e20f299)


## Installation

### Manual Installation
1. Mount your NextUI SD card to a computer.
2. Download the latest release file named `Music.Player.pak.zip` from Github.
3. Copy the zip file to `/Tools/<PLATFORM>/Music.Player.pak.zip` (replace `<PLATFORM>` with your device: `tg5040` or `tg5050`).
4. Extract the zip in place, then delete the zip file.
5. Confirm that there is a `/Tools/<PLATFORM>/Music.Player.pak` folder on your SD card.
6. Rename the `Music.Player.pak` folder to `Music Player.pak`.
7. Unmount your SD Card and insert it into your TrimUI device.

### Pak Store Installation

1. Open `Pak Store` application in your TrimUI device. 
2. Navigate to the `Browse` then `Media` menu. 
3. Select `Music Player` to install.

## Update

1) You can update the application directly via `About` page in the application.
2) Or, you can update via `Pak Store`.

## Features

### General
- Support Bluetooth/USB-C devices for output and media controls.
- Automatic screen off (Follow system screen timeout).

### Music
- Supports `WAV`, `MP3`, `OGG`, `FLAC`, `M4A`, `AAC` and `OPUS` formats
- File browser for navigating music libraries (Audio files must be placed in `./Music` folder)
- Shuffle and repeat modes
- Spectrum visualizer with 4 options of color to choose from.
- Album art display (Automatically download album art if track doesn't provide)
- Automated lyric download and display during playback
- Search and download YouTube Music for music (Downloaded tracks will be placed in `./Music/Download`)
- Playlist management
- A pinned `Continue` row at the top of the Music menu resumes the last track played

### Audiobook
- Books live in `./Audiobook`: either a subfolder (its audio files, sorted by name, are the chapters) or a single file
- `M4B` files with internal chapters (Nero `chpl` or QuickTime chapter tracks)
- One saved position **per book**, so several books can be in progress at once
- A pinned `Continue` row at the top of the library jumps straight back into the last book
- Chapter list with direct jump, skip back 10s / forward 30s
- Sleep timer (15/30/45/60 minutes, or end of chapter)
- Playback continues in the background; "Now Playing" on the main menu returns to it

Two ways to organize a book under `./Audiobook`:

```
Audiobook/
├── Project Hail Mary/              # one folder per book = one chapter per file,
│   ├── 01 - Chapter 1.mp3          # sorted by file name
│   ├── 02 - Chapter 2.mp3
│   └── 03 - Chapter 3.mp3
└── Dune.m4b                        # or a single standalone .m4b with its
                                     # chapters read from embedded metadata
```

### Online Radio
- Preset station management (add, remove, save)
- Curated station browser organized by country (Only Malaysia for now - others will be added later; please suggest)
- Support for `MP3`, `AAC`, Ogg Vorbis, and Ogg Opus streams, direct streaming
  (Shoutcast/Icecast) and `HLS` (m3u8).
- HTTPS support via mbedTLS
- Metadata display (song title, artist, station info)
- Album art display (Downloaded from internet based on current song)

### Podcast
- Search Apple Podcast for podcast source
- Subscription management
- Download episodes for local offline playback
- Podcast files will be placed in `./Podcasts`

## Controls

### Main Menu Navigation
- **D-Pad**: Navigate menus and file browser
- **A Button**: Select/Confirm
- **B Button**: Back/Cancel/Exit
- **X Button**: Stop Background Playback (on the `Now Playing` row)
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application

The first row only appears while audio is playing, and returns to whichever
player owns it. Resuming a stopped track is done from the `Continue` row inside
the `Music` and `Audiobook` menus.

### Music Menu
- **A Button**: Select (on `Continue`, resumes the last track)
- **B Button**: Back
- **X Button**: Forget the `Continue` entry
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application

### Music Player
- **A Button**: Play/Pause
- **B Button**: Back/Cancel/Exit
- **X Button**: Toggle Shuffle
- **Y Button**: Toggle Repeat
- **D-Pad Up**: Next Track
- **D-Pad Down**: Prev Track
- **D-Pad Right**: Fast Foward
- **D-Pad Left**: Rewind
- **Select**: Turn Off Screen
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application
- **L1/R1 Shoulders**: Prev/Next Track
- **L2/L3 Shoulders**: Toggle Visualizer
- **R2/R3 Shoulders**: Toggle Lyrics

### Audiobook Player
- **A Button**: Play/Pause
- **B Button**: Back (audio keeps playing in the background)
- **X Button**: Chapter list
- **Y Button**: Sleep timer (Off -> 15 -> 30 -> 45 -> 60 min -> end of chapter)
- **D-Pad Left**: Back 10 seconds
- **D-Pad Right**: Forward 30 seconds
- **D-Pad Up/Down**: Prev/Next Chapter
- **Select**: Turn Off Screen
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application
- **L1/R1 Shoulders**: Prev/Next Chapter

### Radio Player
- **B Button**: Back/Stop
- **D-Pad Up**: Next Station
- **D-Pad Down**: Prev Station
- **Select**: Turn Off Screen
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application
- **L1/R1 Shoulders**: Prev/Next Station

### Podcast
- **B Button**: Back/Stop
- **Select**: Turn Off Screen
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application

## Usage

### Playing Local Music
- Navigate to your music folder using the `Music` menu
- Select a file to start playback
- The `Continue` row at the top of the `Music` menu picks the last track back up

### Audiobooks
- Copy books into `./Audiobook` on the SD card, one folder per book (or a single `.m4b`)
- Open the `Audiobook` menu; the `Continue` row at the top resumes the last book you were listening to
- Books already in progress are listed first, showing their chapter and position
- Select a book to resume exactly where you left off
- Press `X` in the book list to mark a book finished or unfinished

### Online Radio
- Navigate to the stations list using the `Online Radio` menu
- Press `Y` button to manage stations.
- Or add custom stations at `.userdata/shared/music-player/radio/stations.txt`
- Metadata displays automatically when available

### Podcast
- Navigate to the podcasts list using the `Podcasts` menu
- Select which subscribed podcast you wish to listen.
- Download any episode you wish to listen.
- Once downloaded, it will be available to play.
- Press `Y` button to manage subscriptions.  

## Building from Source

### Prerequisites
- Docker or Podman (device builds run in the NextUI toolchain image)
- `git` (the build dependencies are checked out by the `dev` script)

### Build Dependencies

The player compiles against the dependencies specified in `deps.json`.

```bash
./dev deps fetch   # fetch any missing dependencies (no-op for already fetched ones)
./dev deps update  # updates dependencies to versions specified by deps file
```

`fetch` is a step of `build`, so the first build on a fresh clone will pull
all dependencies automatically.

Dependency file can be updated, the call to `./dev deps update` will try to update
checkouts to match the new version.

You can freely switch dependency to your own revision, and make changes to a dependency,
version tracking will detect that and won't force the pinned version.

### VS Code

Run `./dev deps fetch` to fetch dependencies (mainly NextUI) first, then open
**C/C++: Edit Configurations (UI)** from the VS Code command palette and fill in:

Include path:

```
NextUI/workspace/all/common
NextUI/workspace/desktop/platform
NextUI/workspace/desktop/libmsettings
```

Defines:

```
USE_SDL2=1
PLATFORM=desktop
```

### Build Commands and Tools

```bash
# Build only
./dev build                            # desktop
./dev build brick tsps                 # tg5040 and tg5050

# Run host-side unit tests
./dev test

# Build, install, and run on connected devices
./dev devices
./dev install device
./dev install device dist/Music.Player.pak.zip   # install a prebuilt pak (no build)
./dev run device
./dev log device --follow

# Build a release folder and ZIP package using the distribution manifest
./dev dist all --strict

# Remove generated local build and distribution output
./dev clean
./dev clean --deps                     # also remove the dependency checkouts

# Inspect or update release versions and changelog notes
./dev version latest
./dev version list

# Enter a toolchain or run a command in it
./dev docker 5040
./dev docker 5040 -- ls
./dev docker 5040 --podman -- ls
```

Platform aliases include `brick`, `brickpro`, `tsp`, `smartpro`, `5040`,
`tsps`, `smartpros`, and `5050`. Aliases ignore case, spaces, hyphens, and
underscores. An ADB serial can also be used wherever a device target is
accepted.

By default, install and run only update the binary.
Use `--full` to install the complete pak and `--delete` to remove stale files
from the installed pak before uploading it (userdata is not cleared).
Passing a path to a `.zip` pak alongside a device target skips the build and
installs that archive as a full pak (`--delete` still applies).

### Project Structure

```
nextui-music-player/         # This project
├── NextUI/                  # NextUI checkout (managed through ./dev deps)
│   └── workspace/
│       ├── all/             # Shared code (common utilities, minarch)
│       ├── tg5040/          # TrimUI Brick platform
│       └── tg5050/          # TrimUI Smart Pro S platform
├── src/                     # Source code
├── bin/                     # Platform binaries and runtime tools
│   ├── tg5040/              # TrimUI Brick binary (musicplayer.elf)
│   ├── tg5050/              # TrimUI Smart Pro S binary (musicplayer.elf)
│   ├── yt-dlp               # YouTube downloader (installed on demand)
│   ├── qjs                  # QuickJS, required by yt-dlp (installed on demand)
│   ├── ffmpeg               # media convertor, required by yt-dlp (installed on demand)
│   └── keyboard             # On-screen keyboard
├── res/                     # Resources (fonts, images, CA bundle)
├── stations/                # Curated radio stations
└── state/                   # Runtime state files
```

### Dependencies

The music player uses:
- **Shared code**: `NextUI/workspace/all/common/` (utils, api, config, scaler)
- **Platform code**: `NextUI/workspace/<PLATFORM>/platform/`
- **Libraries**: SDL2, SDL2_image, SDL2_ttf, GLESv2, EGL, libsamplerate, libzip, mbedTLS, ALSA
