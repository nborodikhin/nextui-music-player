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

### Library
- Supports `WAV`, `MP3`, `OGG`, `FLAC`, `M4A`, `AAC` and `OPUS` formats
- File browser for navigating music libraries (Audio files must be placed in `./Music` folder)
- Shuffle and repeat modes
- Spectrum visualizer with 4 options of color to choose from.
- Album art display (Automatically download album art if track doesn't provide)
- Automated lyric download and display during playback
- Search and download YouTube Music for music (Downloaded tracks will be placed in `./Music/Download`)
- Playlist management

### Online Radio
- Preset station management (add, remove, save)
- Curated station browser organized by country (Only Malaysia for now - others will be added later; please suggest)
- Support for `MP3` and `AAC` streams, direct streaming (Shoutcast/Icecast) and `HLS` (m3u8).
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
- **X Button**: Clear Resume History/Background Playback
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
- Navigate to your music folder using the `Library` menu
- Select a file to start playback

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
- Cross-compilation toolchain for ARM64
- A NextUI workspace checkout with platform dependencies (`all/`, `tg5040/`,
  `tg5050/`) somewhere on disk
- A `.nextui-workspace` symlink at this repo's root pointing at that
  checkout's `workspace/` directory, e.g.:
  ```bash
  ln -s /path/to/NextUI/workspace .nextui-workspace
  ```
  `run-docker.sh` (used by `build-tg5040.sh`, `build-tg5050.sh`, `dev.sh`, and
  `deploy.sh`) mounts that workspace and bind-mounts this repo over its
  `nextui-music-player/` slot, so builds always use whatever checkout or
  worktree you're actually running from — no need to physically nest this
  repo inside the NextUI tree. It fails fast with a clear error if the
  symlink is missing or broken.

### Build Commands

```bash
# Enter the toolchain (replace PLATFORM accordingly) 
make shell PLATFORM=tg5040

# Once in the toolchain shell
cd ~/workspace/nextui-music-player/src

# Build for TrimUI Brick (tg5040)
make clean && make PLATFORM=tg5040
```

### Packaging a Release Folder

Once binaries are built (`sh build-tg5040.sh` / `sh build-tg5050.sh`), assemble a
deployable pak folder locally:

```bash
python3 create_dist.py            # both platforms -> dist/Music Player.pak/
python3 create_dist.py --zip      # also write dist/Music.Player.pak.zip
```

Copy `dist/Music Player.pak/` straight to `/Tools/<PLATFORM>/` on the SD card —
no zip/extract/rename round trip needed. The script only copies the runtime
payload (`bin/`, `res/`, `state/`, `stations/`, `pak.json`, `launch.sh`), so
source, docs, and build scripts never leak into the shipped pak. Run
`python3 create_dist.py --help` for platform selection and other flags.

### Iterating on a Connected Device

Two scripts wrap the build + adb push cycle, for different points in a change:

```bash
sh dev.sh [tg5040|tg5050]      # code-only iteration: build, push the binary,
                                # kill + relaunch on-device, tail its log
sh deploy.sh [tg5040|tg5050|all]  # full resync: build, package via
                                   # create_dist.py, push the whole pak folder
```

`dev.sh` is the tight loop while hacking on `src/*.c` — it only refreshes
`musicplayer.elf` on top of a pak that's already installed, so it's fast but
assumes `res/`, `stations/`, `pak.json`, etc. are already current on the card.
`deploy.sh` pushes the complete folder (same payload `create_dist.py`
produces), so run it after touching anything besides source, or before
handing a build to someone else to test. Both require `adb` connected to the
device. The relaunch/log-tail steps in `dev.sh` assume standard NextUI SD
card paths — adjust `PAK_DIR`/log discovery in the script if your setup
differs.

### Project Structure

```
workspace/
├── nextui-music-player/     # This project
│   ├── src/                 # Source code
│   ├── bin/                 # Platform binaries and runtime tools
│   │   ├── tg5040/          # TrimUI Brick binary (musicplayer.elf)
│   │   ├── tg5050/          # TrimUI Smart Pro S binary (musicplayer.elf)
│   │   ├── yt-dlp           # YouTube downloader
│   │   ├── wget             # HTTP downloader
│   │   └── keyboard         # On-screen keyboard
│   ├── res/                 # Resources (fonts, images)
│   ├── stations/            # Curated radio stations
│   └── state/               # Runtime state files
├── all/                     # Shared code
│   ├── common/              # Common utilities, API
│   └── minarch/             # Emulator framework
├── tg5040/                  # TrimUI Brick platform
│   ├── platform/            # Platform-specific code
│   └── libmsettings/        # Settings library
└── tg5050/                  # TrimUI Smart Pro platform
    ├── platform/            # Platform-specific code
    └── libmsettings/        # Settings library
```

### Dependencies

The music player uses:
- **Shared code**: `workspace/all/common/` (utils, api, config, scaler)
- **Platform code**: `workspace/<PLATFORM>/platform/`
- **Libraries**: SDL2, SDL2_image, SDL2_ttf, GLESv2, EGL, libsamplerate, libzip, mbedTLS, ALSA
