# NextUI Music Player
A comprehensive music playback application for NextUI featuring local file playback, online radio streaming, podcast and music downloading.

> **Note:**
>
> This is a fork of [mohammadsyuhada/nextui-music-player](https://github.com/mohammadsyuhada/nextui-music-player)<br>
> [Support](https://ko-fi.com/Y8Y61SI04B) the original author on ko-fi.

## Supported Platforms
- **tg5040** - TrimUI Smart Pro / TrimUI Brick / Brick Hammer
- **tg5050** - TrimUI Smart Pro S
- **my355** - Miyoo Flip
- **h700** - Anbernic (H700)

![music_player](https://github.com/user-attachments/assets/de4fe612-1c48-4e98-9537-79504e20f299)


## Installation

### Manual Installation

Each release has one `.pakz` package for each platform:

| Device                         | Package                    |
|--------------------------------|----------------------------|
| TrimUI Brick, TrimUI Smart Pro | `Music.Player-tg5040.pakz` |
| TrimUI Smart Pro S             | `Music.Player-tg5050.pakz` |
| Miyoo Flip                     | `Music.Player-my355.pakz`  |
| Anbernic RG XX (H700) devices  | `Music.Player-h700.pakz`   |

1. Mount your NextUI SD card to a computer.
2. Download the `.pakz` package for device from the latest release on Github.
3. Copy the package to the root of the SD card
  - You can put several `.pakz` files if you use SD card on several devices.
4. Unmount your SD card and insert it into your device.
5. Start the device. NextUI installs each package and then deletes it.

### Pak Store Installation

1. Open `Pak Store` application in your TrimUI device. 
2. Navigate to the `Browse` then `Media` menu. 
3. Select `Music Player` to install.

## Update

1) You can update the application directly via `About` page in the application.
2) Or, you can update via `Pak Store`.

## Features

### General
- Complete support of NextUI theme and power settings.
- Support Bluetooth/USB-C devices for output and media controls.
- Automatic screen off.

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

[DEV_README.md](DEV_README.md) tells how to build, run and test the player, and how its code
is organized.
