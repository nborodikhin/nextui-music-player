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

### Driving the App from a Script

The app allows programmatic interaction via option `--test-control=<in>[,<out>]`.
`<in>` is the source of the commands and `<out>` is the destination
of the replies. Each side is one of these forms.

| Form | Meaning |
|---|---|
| `std` | Standard input for `<in>`, standard output for `<out>` |
| `fd:<n>` | A descriptor that the program which started the app opened |
| A path | A regular file or a FIFO |

If `<out>` is absent, the replies go to standard output. Each reply starts with
`@`, to differentiate output from app's own log messages. A program
with one bidirectional channel, such as a socket pair, may use that descriptor
for both sides: `--test-control=fd:3,fd:3`.

Each line of the script is one step. The commands of one line start together,
and the next line starts only after the current line completes. A comma between
two commands is permitted and has no effect. Spaces are permitted around each
command and each argument. A `#` starts a comment.

| Command            | Effect                                                   |
|--------------------|----------------------------------------------------------|
| `press(BTN)`       | One press and release                                    |
| `press(BTN, n)`    | `n` presses and releases                                 |
| `hold(BTN, ms)`    | Press a button and keep it pressed for `ms` milliseconds |
| `hold(BTN, keep)`  | Press a button and keep it pressed until `release(BTN)`  |
| `release(BTN)`     | Release a button that `hold(BTN, keep)` put down         |
| `wait(ms)`         | A delay                                                  |
| `screenshot(path)` | Take a PNG screenshot and save it into the file          |
| `quit()`           | Exit the app (also see EOF note below)                   |
| `keep()`           | End the script, do not exit the app                      |

`BTN` is one of `UP`, `DOWN`, `LEFT`, `RIGHT`, `A`, `B`, `X`, `Y`, `START`,
`SELECT`, `L1`, `R1`, `L2`, `R2`, `MENU`, `PLUS`, `MINUS`, `POWER`.

The app writes `@ok <line>` when a step completes, `@err <line> <message>` for a
command that it cannot execute, and `@bye` before it stops. A harness waits for
the `@ok` of the last line it sent, rather than for an estimated delay.

A regular file, standard input and a descriptor end the run at their end of
file, after the app completes each step that it received. A FIFO does not end
the run, thus a script that uses a FIFO must use explicit `quit()`.

`hold(BTN, keep)` puts a button down and leaves it down. The button repeats
in the same way as a button that a person holds, and it stays down through each
step that follows, until `release(BTN)`. This is how a script makes an image, or
presses other buttons, while a button stays down:

```
hold(L1, keep)
press(DOWN), press(DOWN)
screenshot(shots/with-l1-down.png)
release(L1)
```

A script that ends with `keep()` leaves the app in operation after the end of
its source. This leaves the app on the screen that the script made, for
examination by eye or for input from the hardware. Such a run stops with
`quit()` or with a signal.

An example that opens the library and makes two images. Start the app from the
directory that holds `res/`, because the app reads its resources by a relative
path:

```bash
cat > case.txt <<'EOF'
wait(2500)                      # the app starts its interface
screenshot(shots/01-menu.png)
press(A)                        # open the row under the cursor
wait(1200)
press(A)                        # open Files
wait(1500)
hold(DOWN, 500)                 # the automatic repeat moves 3 rows
wait(400)
screenshot(shots/02-files.png)
quit()
EOF

mkdir -p shots
./bin/desktop/musicplayer.elf --test-control=case.txt
```

A FIFO could be used for an interactive app operation, based on the analysis
of the data received from the program.

```bash
mkfifo cmds
./bin/desktop/musicplayer.elf --test-control=cmds,replies.log &
echo 'screenshot(shots/03.png)' > cmds
# if screenshot shows an expected state
echo 'press(DOWN), press(A)' > cmds
echo 'quit()' > cmds
```

The image shows what the app draws to its surface. The title that scrolls and
the time of the track go to a graphics layer of the platform, thus they are not
in the image.

The same option operates a device build. The app also continues to accept the
buttons of the hardware during a run.


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
