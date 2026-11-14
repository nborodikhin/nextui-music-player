# NextUI Music Player - Developer Guide

## Building from Source

### Prerequisites
- Docker or Podman (device builds run in the NextUI toolchain image)
- `git` (the build dependencies are checked out by the `dev` script)

### Build Dependencies

The player compiles against the dependencies specified in `deps.json`.

```bash
./dev deps fetch   # fetch any missing dependencies (no-op for already fetched ones)
./dev deps update  # updates dependencies to versions specified by deps file
./dev deps fetch --origin ../nextui-music-player   # take NextUI and Zig from that checkout
```

`fetch` is a step of `build`, so the first build on a fresh clone will pull
all dependencies automatically.

`deps.json` also pins Zig, which compiles the C++ code and links the binary of
each build, the desktop and the unit tests too. `fetch` downloads the Linux Zig
for the CPU of the host into `deps/zig/`, which the toolchain images run. On
macOS it also downloads the macOS Zig for the desktop build. It checks the
sha256 of each archive, and its minisign signature where `minisign` is
installed. It keeps each archive in `deps/downloads/`, thus a fetch again
downloads nothing.

A secondary checkout, such as a git worktree, takes its dependencies from an
origin checkout. `fetch` makes each NextUI tree a git worktree of the NextUI
clone of the origin, thus it clones no history, and it unpacks Zig from the
archive that the origin keeps. In a linked git worktree of this repository, the
origin is the main worktree. `--origin <checkout>` names another origin, and
`--origin none` clones and downloads in the checkout itself. `./dev clean --deps`
removes a NextUI worktree through git, and keeps a NextUI clone that has
worktrees.

A dependency can name a directory of patches, which `fetch` applies to the
checkout. `deps/patches/nextui/` holds them for the NextUI trees: each one is a
pull request that is open upstream, and it goes away when the pull request is
merged and the pin moves. The pin marker records the patch set, thus a build
tells a patched checkout from one with hand edits and warns only about the
second.

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

### CLion

Open the project root as a Makefile project. CLion collects the compiler
flags from a dry run of one make goal, and the default goal builds `src/`
only, thus symbols in `test/` do not resolve and *Find Usages* does not
find them. Point CLion at the `ide` goal, which also compiles the unit
tests.

CLion knows a compiler by the name of the program that runs, and it does not
know `zig`. Thus the Makefiles compile C++ with `zig-c++`, a script that
`./dev deps fetch` puts beside `zig` and that runs `zig c++`.

The default goal runs `./dev build desktop`, also in a dry run. Thus the
reload of the project fetches the dependencies and prepares the NextUI tree
when necessary. The first reload on a fresh clone takes some minutes.

1. **Settings | Build, Execution, Deployment | Makefile**: set
   **Build target** to `ide`, and set **Arguments** to `--always-make`.
   After a build, a dry run of make shows no commands for the files that
   are up to date. Then CLion reports "This file does not belong to any
   project target" for those files. `--always-make` makes the dry run show
   all compile commands.
2. **Tools | Makefile | Reload Makefile Project**.

### Build Commands and Tools

```bash
# Build only
./dev build                            # desktop
./dev build brick tsps                 # tg5040 and tg5050
./dev build h700                       # Anbernic H700

# Run host-side unit tests
./dev test

# Run the desktop app
./dev run desktop
./dev run desktop --headless -- --test-control case.txt,std
./dev run desktop --sdcard runs/a      # own sdcard and /tmp, needs bwrap
./dev run desktop --sdcard runs/b --sdcard-template seed

# Build, install, and run on connected devices
./dev devices                          # the app, the pak version and the paths
./dev install device
./dev install device dist/Music.Player.pak.zip   # install a prebuilt pak (no build)
./dev run device
./dev log device --follow
./dev kill -r                          # restart the app where it runs

# Examine the one connected device. The words pakdir, userdata, db and log
# change to the paths of the app on the device.
./dev device ls -la userdata
./dev device pull db saved.db
./dev device push saved.db db          # stop the app first with ./dev kill
./dev device rm -f log
./dev device shell 'ls /mnt | head -1'
./dev shell                            # same as ./dev device shell

# Build the pak zip and one .pakz package for each platform, using the
# distribution manifest
./dev dist all --strict

# Remove generated local build and distribution output
./dev clean
./dev clean --deps                     # also remove the dependency checkouts

# Inspect or update release versions and changelog notes
./dev version get                     # local build version: v1.17.0+local
./dev version get --identifier abc1234   # v1.17.0+abc1234
./dev version validate-release v1.18.0   # exit status 0 if the tag can be a release
./dev version latest
./dev version list

# Help
./dev --help
./dev --help --all

# Enter a toolchain or run a command in it
./dev docker 5040
./dev docker 5040 -- ls
./dev docker 5040 --podman -- ls
```

For a stable release, use `./dev version create vMAJOR.MINOR.PATCH <note>` to
prepare `pak.json`, then commit that preparation and push the matching tag.
For a pre-release, push a tag such as `v1.18.0-beta.1`. Do not run
`version create`. A pre-release keeps the version and changelog in `pak.json`
of the latest stable release.

Each build command accepts `--version <value>`. Without it, the binary holds
the highest official tag that HEAD contains, with `+local`. Without an official
tag, it uses the stable version in `pak.json`, with `+local`.

### Build Kinds

The build makes one of two kinds.

| Kind | Optimization | Debug symbols | Control channel |
|---|---|---|---|
| `--debug` | `-O2` | yes | yes |
| `--release` | `-O2` | no, the binary is stripped | no |

Both kinds optimize the same. Thus a fault that the optimizer causes is in both,
and the debug kind shows the behavior of the release kind.

`./dev build`, `./dev install` and `./dev run` make the debug kind unless you
pass `--release`. `./dev dist` makes the release kind unless you pass `--debug`.

```bash
./dev build brick                      # debug: symbols and the control channel
./dev build brick --release            # what the pak ships
./dev dist all --strict                # release
./dev dist all --strict --debug        # a strict pak with symbols and the channel
```

A driven test needs the debug kind. A release binary does not accept
`--test-control`, and its `--help` does not name the option or the commands of
the channel. `--strict` checks the dependencies and the contents of the package,
not the kind.

Each kind keeps its object files under `src/build/<kind>/`, so switching kinds
does not rebuild the tree. Both kinds write the same binary path, and the build
prints the kind on every target.

Platform aliases include `brick`, `brickpro`, `tsp`, `smartpro`, `5040`,
`tsps`, `smartpros`, and `5050`. Aliases ignore case, spaces, hyphens, and
underscores. An ADB serial can also be used wherever a device target is
accepted.

By default, install and run only update the binary.
Use `--full` to install the complete pak and `--delete` to remove stale files
from the installed pak before uploading it (userdata is not cleared).
Passing a path to a `.zip` pak alongside a device target skips the build and
installs that archive as a full pak (`--delete` still applies).
`--delete-data` removes the data of the app (settings, database, playlists,
podcasts) from `.userdata/shared/music-player` on the device before the install.
It does not remove the pak, thus combine it with `--delete` for a clean install.

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
| `sql(sql)`         | Execute SQL on the app thread                            |
| `quit()`           | Exit the app (also see EOF note below)                   |
| `keep()`           | End the script, do not exit the app                      |

`BTN` is one of `UP`, `DOWN`, `LEFT`, `RIGHT`, `A`, `B`, `X`, `Y`, `START`,
`SELECT`, `L1`, `R1`, `L2`, `R2`, `MENU`, `PLUS`, `MINUS`, `POWER`, or a
direction of the analog stick: `STICK_UP`, `STICK_DOWN`, `STICK_LEFT`, `STICK_RIGHT`.

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

The image shows what the display shows: the app composes its surface with the
graphics layers of the platform, thus the title that scrolls, the time of the
track, the lyrics, the spectrum and each toast are in the image. A NextUI tree
that cannot compose gives the surface alone, and the layers are then absent.

The same option operates a device build. The app also continues to accept the
buttons of the hardware during a run.

Each desktop run reads and writes the shared sdcard `/var/tmp/nextui/sdcard`, thus
two runs at one time change the data of each other. `./dev run desktop --sdcard
<dir>` runs the app in a mount namespace of bwrap, with `<dir>` in place of the
shared sdcard and with a private `/tmp`. `.system`, `Tools` and `Music` come from
the shared sdcard, read-only. `--sdcard-template <template>` copies `<template>`
into `<dir>` when `<dir>` is missing or empty. A path in the script must be
outside `/tmp`. Without bwrap, as on macOS, `--sdcard` has no effect and `dev`
warns.


### Project Structure

```
nextui-music-player/         # This project
├── NextUI/                  # NextUI checkout (managed through ./dev deps)
│   └── workspace/
│       ├── all/             # Shared code (common utilities, minarch)
│       ├── tg5040/          # TrimUI Brick platform
│       ├── tg5050/          # TrimUI Smart Pro S platform
│       └── h700/            # Anbernic H700 platform
├── src/                     # Source code
│   └── ui/                  # Pages and the page loop
│       └── view/            # UI framework: views and the compositor
├── bin/                     # Platform binaries and runtime tools
│   ├── tg5040/              # TrimUI Brick binary (musicplayer.elf)
│   ├── tg5050/              # TrimUI Smart Pro S binary (musicplayer.elf)
│   ├── h700/                # Anbernic H700 binary (musicplayer.elf)
│   ├── yt-dlp               # YouTube downloader (installed on demand)
│   ├── qjs                  # QuickJS, required by yt-dlp (installed on demand)
│   ├── ffmpeg               # media convertor, required by yt-dlp (installed on demand)
│   └── keyboard             # On-screen keyboard
├── deps/
│   ├── patches/             # Patches that ./dev deps fetch applies to a checkout
│   └── zig/                 # Zig, which ./dev deps fetch downloads (not in git)
├── res/                     # Resources (fonts, images, CA bundle)
├── stations/                # Curated radio stations
├── state/                   # Runtime state files
└── Makefile                 # Forwards to src/; the `ide` goal also builds test/
```

### Dependencies

The music player uses:
- **Shared code**: `NextUI/workspace/all/common/` (utils, api, config, scaler)
- **Platform code**: `NextUI/workspace/<PLATFORM>/platform/`
- **Libraries**: SDL2, SDL2_image, SDL2_ttf, GLESv2, EGL, libsamplerate, libzip, mbedTLS, ALSA
- **Sonic** (`src/audio/sonic.c`, Apache-2.0): the change of playback speed that keeps the pitch
- **Pak libraries**: where the firmware lacks a library, the pak carries one in
  `bin/<platform>/` and `launch.sh` loads it before `/usr/lib`. The build copies
  each one out of a toolchain image (`PAK_LIBRARIES` in `dev`); none is in git.
  Today: libfdk-aac on tg5050, my355, and h700, and SDL2_image 2.9 (JPEG decoder) on my355 and h700.

## UI Framework

The code in `src/ui/view/` draws a screen from views. A page (`UiPage` in
`src/ui/ui_page.hpp`) owns its views, and the page loop (`runPageLoop()`) runs
its frames. The main menu and the input test page use this framework. The other
screens use the older draw functions.

### Parts

| Type | What it does |
|---|---|
| `UiView` | A part of a screen. It lays itself out on each frame, and draws into a bitmap when its content changes. |
| `UiLayout` | A view that only places its children, and draws nothing of its own. |
| `UiLayoutContext` | The entry of one view in one frame: its place, its size, its content rectangle, its clip, its target layer and its visibility. |
| `UiDrawContext` | The bitmap of one view during its draw. |
| `UiContext` | What a view needs from the screen: the pixel format and the size. |
| `UiCompositor` | Keeps one bitmap for each view, and composes the bitmaps on the screen. |

`TextView`, `TitleTextView`, `ListRowView`, `ListView`, `ScrollIndicatorView`,
`StatusGroupView`, `ButtonHintsView` and `BoxView` are the views. `ListScreenView`
is a menu screen with a list. It owns the title, the status group, the list, the
scroll indicators and the button hints, and lays them out. The page gives the list
its adapter and keeps the adapter.

### The Frame

The page loop does these steps in each frame:

1. It waits for the time of the frame (`GFX_sync()`), and then presents the
   result of the last frame. The present comes at the same point in each frame,
   thus a scrolling text moves in equal steps. The effect of an input shows one
   frame later.
2. It reads the input, and sends the events to the page.
3. It calls `onTick()` of the page. The page changes the state of its views
   here: a text, a selection, the time of a scrolling text.
4. It calls `layout()` of the page.
5. The compositor draws the views that changed, and composes the regions of the
   screen that changed.

A frame changes the regular surface, the layers, or nothing. The present sends
only what changed. A frame where only a scrolling text moves changes the
animation layer only, thus the present does not upload the surface.

### Layout

The layout runs on each frame, thus it must be cheap. A layout puts each view in
its place and records the entries of the frame. It does not draw. A view keeps
its expensive work, such as the render of a text, for the moment that its
content changes, and keeps the result.

- A parent calls `context.layoutChild(childView, rect)`, or
  `context.layoutChild(childView, rect, clip)`. `rect` is in the coordinates of
  the parent, and its size is the max size of the child. The call returns the
  context of the child, which stays valid until the `layout()` of the parent
  returns.
- `setSize()` sets the size of the view and of its bitmap. The default is the max
  size.
- `setTargetLayer()` puts the view on the regular surface or on the animation
  layer. The default is the target layer of the parent.
- A child is clipped to the rectangle of its parent, and to `clip`.
- The order of the `layoutChild()` calls is the z order. A later view is on top.
  The bitmap of a view is below the bitmaps of its children.
- A view that only places children is a `UiLayout`. A layout that needs a
  background adds it as a child, thus the order of the background and the
  children is clear.
- A view has two kinds of visibility. `UiView::setVisible()` hides the view until
  the next call: its owner decides it. `UiLayoutContext::setVisible()` hides the
  view in this frame: its parent decides it, such as a list that hides a row
  that does not fit.
- `createMeasuringContext()` returns a context in which a layout adds no entries.
  A view uses it to get the size of a child before it places the child.
- A view that the layout does not reach in a frame loses its bitmap. A hidden
  view keeps its bitmap.

### Draw

The compositor calls `draw()` of a view only when the view is dirty
(`isDirty()`), when its size changed, or when it was never drawn. A view that
moves and does not change is not drawn again.

- `draw()` draws the whole view into `context.getSurface()`, in the coordinates
  of the view. The bitmap has the size of the view, and is transparent when the
  call returns it. The result must not depend on the position of the view.
- `context.setContentRect()` sets the part of the bitmap that holds content. The
  default is the whole bitmap. The compositor composes only this part.
- A draw that does not call `getSurface()` leaves the view with no content. The
  view then has no bitmap, and the compositor does not compose it.
- The bitmap holds premultiplied alpha. Blit a source with straight alpha with
  `SDL_BLENDMODE_BLEND`: a blend onto a transparent pixel gives a premultiplied
  pixel. Copy a premultiplied source with `SDL_BLENDMODE_NONE`.
- `draw()` clears the dirty flags of the view.

### Composition

After the layout, the compositor reads the entries in z order, in two passes.

1. The first pass picks the shown views to draw: the views that are dirty, that
   changed their size, or that were never drawn.
2. The compositor draws these views. A draw can change the content rectangle,
   thus the damage comes after it.
3. The second pass compares each entry with the last frame. The place of the
   content of an entry comes from its bitmap. The pass records a damaged region
   for each change:

   - a new view, or a view that it drew again: the visible content of the view;
   - a view that moved, changed its content rectangle, its clip or its target
     layer: the old place on the old target, and the new place on the new
     target;
   - a view that went away, is hidden, or has no content now: its last place.

Each target layer has its own list of damaged regions. Regions that overlap join
into one, thus each pixel composes one time.

A change of the z order alone gives no damage. When two views that overlap
change their order in the layout and nothing else changes, the overlap keeps the
old order until another change damages it. Mark one of the views dirty when the
order changes.

Then the compositor composes each damaged region:

1. It starts from a transparent buffer of the size of the region.
2. It blends the bitmap of each shown view of that target over the buffer, in z
   order, with a premultiplied blend: `dst = src + dst * (1 - src.alpha)`.
3. It sends the buffer to its target, in the alpha form that the platform blends
   that target with. A table in the compositor gives the form of each target:

   | Target               | Platform blend                   | Buffer it gets |
   |----------------------|----------------------------------|----------------|
   | Background (layer 1) | Replace                          | Premultiplied  |
   | Selection (layer 2)  | Premultiplied                    | Premultiplied  |
   | Regular surface      | Premultiplied                    | Premultiplied  |
   | Status (layer 3)     | Straight (`SDL_BLENDMODE_BLEND`) | Straight       |
   | Animation (layer 4)  | Premultiplied                    | Premultiplied  |
   | Toast (layer 5)      | Straight (`SDL_BLENDMODE_BLEND`) | Straight       |

   The background layer replaces what is below it, thus a translucent pixel on it
   shows as over black. A region of a layer goes to `UiLayer_blit()`, which
   replaces that region of the layer.

### Layer Transition

A view goes to the other target when its layout calls `setTargetLayer()`. The
compositor then composes the old place on the old target and the new place on
the new target, in the same frame. `TextView` uses this: a text at rest is on
the regular surface, and a text that scrolls is on the animation layer. Thus a
frame where a text scrolls composes the layer only.

The animation layer is above the regular surface. A view on the surface does not
cover a view on the layer, also when it is later in the z order.

### Screen Changes and Dialogs

- A new screen surface or a new output calls `setContext()` of the page with the
  compositor. Each view drops its caches, and the compositor composes the whole
  screen again.
- After a sleep, `invalidateAll()` composes the whole screen again.
- Before a dialog, the loop calls `pause()` of the page and puts the compositor
  in surface-only mode. In this mode each target is the regular surface, and the
  compositor clears each layer that it wrote. The frame then shows the whole page on the
  surface, with each scrolling text stopped where it is, and the dialog draws
  over it. After the dialog, the loop calls `resume()` and composes the whole
  screen again.
- When the page exits, the compositor clears each layer that it wrote, thus the
  next screen starts with no content of the page on the layers. A layer that the
  compositor did not write keeps its content.
