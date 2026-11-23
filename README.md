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

A `Music.Player-debug-<platform>.pakz` package holds a debug build, which writes
more lines to the log. Install it only to help find the cause of a problem.

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
- Supports `WAV`, `MP3`, `OGG`, `FLAC`, `M4A`, `M4B`, `AAC` and `OPUS` formats
- File browser for navigating music libraries (Audio files must be placed in `./Music` folder)
- Shuffle and repeat modes
- Spectrum visualizer with 4 options of color to choose from.
- Album art display (Automatically download album art if track doesn't provide)
- Automated lyric download and display during playback
- Search and download YouTube Music for music (Downloaded tracks will be placed in `./Music/Download`)
- Playlist management

### Audiobook
- Books live in `./Audiobook`: either a subfolder (its audio files, sorted by name, are the chapters) or a single file
- `M4B` files with internal chapters (Nero `chpl` or QuickTime chapter tracks)
- One saved position **per book**, so several books can be in progress at once
- A `Continue` row at the top of the list jumps straight back into the last book
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
└── Dune.m4b                        # or a single .m4b with its chapters
                                    # read from embedded metadata
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

### Audiobook Player
- **A Button**: Play/Pause
- **B Button**: Back (a playing book keeps playing in the background; a paused one stops)
- **X Button**: Chapter list
- **Y Button**: Sleep timer (Off -> 15 -> 30 -> 45 -> 60 min -> end of chapter)
- **D-Pad Left/Right**: Back 10 seconds / Forward 30 seconds
- **D-Pad Up/Down**, **L1/R1**: Previous/Next chapter
- **Select**: Turn Off Screen (**Select + A** wakes it)
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application

In the book list, **X** marks a book finished or unfinished.

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

### Audiobooks
- Copy books into `./Audiobook` on the SD card, one folder per book (or a single `.m4b`)
- Open the `Audiobook` menu; the `Continue` row at the top resumes the last book you were listening to
- Books in progress are listed first, with their chapter and position
- Select a book to resume exactly where you left off

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
