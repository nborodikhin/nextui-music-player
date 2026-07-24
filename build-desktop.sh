#!/bin/sh
# Build and run the Music Player natively on the host (Linux/macOS) for local
# development — no device, no Docker, no cross-compiler.
#
# Requires the NextUI `desktop` workspace to be prepared first (fake SD root +
# libmsettings.so under /var/tmp/nextui). This script does that setup once,
# the first time it's run; if any step of it fails, nothing is marked done,
# so the next run retries the whole setup from scratch.
#
# This script is local-only (not shipped in the pak). Usage:
#   sh build-desktop.sh            # build, then launch
#   sh build-desktop.sh --build    # build only, don't run
set -e

PLATFORM=desktop
HERE=$(cd "$(dirname "$0")" && pwd)
NEXTUI_ROOT=$(cd "$HERE/../.." && pwd)

# --- native "toolchain" (see NextUI/makefile.native) -----------------------
case "$(uname -s)" in
    Linux)
        export CROSS_COMPILE=/usr/bin/
        export PREFIX=/usr
        ;;
    Darwin)
        export CROSS_COMPILE=/usr/local/bin/
        export PREFIX=/opt/homebrew
        ;;
    *)
        echo "Unsupported host OS: $(uname -s)" >&2
        exit 1
        ;;
esac
export PREFIX_LOCAL=/var/tmp/nextui
export UNION_PLATFORM=$PLATFORM

# --- one-time desktop workspace setup ---------------------------------------
# Marker is only written after every step below succeeds, so a failure
# (e.g. declined sudo, missing brew package) leaves it unset and the full
# setup re-runs next time instead of silently skipping the broken step.
SETUP_MARKER="$PREFIX_LOCAL/.desktop_setup_ok"
if [ ! -f "$SETUP_MARKER" ]; then
    echo "Preparing NextUI desktop workspace (one-time setup)..."

    if [ "$(uname -s)" = "Darwin" ] && [ ! -x /usr/local/bin/gcc ]; then
        sudo "$NEXTUI_ROOT/workspace/desktop/macos_create_gcc_symlinks.sh"
    fi

    "$NEXTUI_ROOT/workspace/desktop/prepare_fake_sd_root.sh"

    make -C "$NEXTUI_ROOT" build PLATFORM="$PLATFORM"

    mkdir -p "$PREFIX_LOCAL"
    touch "$SETUP_MARKER"
    echo "Desktop workspace setup complete."
fi

# --- build ------------------------------------------------------------------
make -C "$HERE/src" PLATFORM="$PLATFORM"

[ "$1" = "--build" ] && exit 0

# --- run --------------------------------------------------------------------
# Resources (res/font.ttf etc.) are resolved relative to the cwd, and the
# on-device paths are hardcoded to SDCARD_PATH=/var/tmp/nextui/sdcard, so create
# the userdata tree the app expects and launch from the project root.
export LD_LIBRARY_PATH="$PREFIX_LOCAL/lib:$LD_LIBRARY_PATH"
export DYLD_LIBRARY_PATH="$PREFIX_LOCAL/lib:$DYLD_LIBRARY_PATH"
mkdir -p /var/tmp/nextui/sdcard/.userdata/shared
mkdir -p /var/tmp/nextui/sdcard/.userdata/desktop

cd "$HERE"
exec ./bin/desktop/musicplayer.elf
