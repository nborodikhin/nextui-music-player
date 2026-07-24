#!/bin/sh
# Run a command inside the NextUI cross-compile toolchain Docker container.
#
# Mounts the entire NextUI workspace (this repo's PARENT directory) at
# /root/workspace — not just this repo — because the Music Player Makefile
# references sibling directories (../../all, ../../tg5040/platform,
# ../../$(PLATFORM)/libmsettings, ...), so the whole workspace must be visible
# inside the container. The toolchain image's workdir is /root/workspace, so
# commands are given workspace-relative (e.g. nextui-music-player/src).
#
# PLATFORM (default tg5040) selects the toolchain image. Usage:
#   sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5040'
#   PLATFORM=tg5050 sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5050'
PLATFORM=${PLATFORM:-tg5040}
WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
exec docker run -it -v "$WORKSPACE":/root/workspace --rm \
    "ghcr.io/loveretro/${PLATFORM}-toolchain" "$@"
