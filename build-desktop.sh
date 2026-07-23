#!/bin/sh
# Build and run the Music Player natively on the host (Linux/macOS) for local
# development — no device, no Docker, no cross-compiler.
#
# Requires the NextUI `desktop` workspace to be prepared first (libmsettings.so
# installed under /var/tmp/nextui). Mirrors the toolchain vars from
# NextUI/makefile.native and the runtime env from NextUI/.env_desktop.
#
# This script is local-only (not shipped in the pak). Usage:
#   sh build-desktop.sh            # build, then launch
#   sh build-desktop.sh --build    # build only, don't run
set -e

PLATFORM=desktop
HERE=$(cd "$(dirname "$0")" && pwd)

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

if [ ! -f "$PREFIX_LOCAL/lib/libmsettings.so" ]; then
    echo "libmsettings.so not found under $PREFIX_LOCAL/lib." >&2
    echo "Build the NextUI desktop workspace first (make PLATFORM=desktop)." >&2
    exit 1
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
