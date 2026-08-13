#!/bin/sh
# Run a command inside the NextUI cross-compile toolchain Docker container.
#
# The Music Player Makefile references sibling directories (../../all,
# ../../$(PLATFORM)/platform, ../../$(PLATFORM)/libmsettings), so those must
# be visible inside the container alongside this repo. Rather than requiring
# this repo to be physically nested inside a NextUI workspace checkout (which
# breaks for git worktrees -- the worktree lives elsewhere on disk), a
# `.nextui-workspace` symlink at the repo root points at the real NextUI
# workspace directory. Its `all/` and `$PLATFORM/` subdirectories are mounted
# as siblings of this repo directly under /root/workspace (the toolchain
# image's WORKDIR), rather than mounting the whole workspace root and
# overlaying this repo inside it -- nesting one bind mount inside another
# trips up some container runtimes (e.g. crun/Podman refuse to create the
# inner mount point). This way whatever checkout/worktree you're running from
# is what actually gets built.
#
# PLATFORM (default tg5040) selects the toolchain image. Usage:
#   sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5040'
#   PLATFORM=tg5050 sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5050'
set -e
PLATFORM=${PLATFORM:-tg5040}
HERE=$(cd "$(dirname "$0")" && pwd -P)
LINK="$HERE/.nextui-workspace"

if [ ! -e "$LINK" ]; then
    if [ -L "$LINK" ]; then
        echo "ERROR: $LINK is a broken symlink (target does not exist)." >&2
    else
        echo "ERROR: $LINK is missing." >&2
        echo "Create it pointing at your NextUI workspace directory, e.g.:" >&2
        echo "  ln -s /path/to/NextUI/workspace \"$LINK\"" >&2
    fi
    exit 1
fi

WORKSPACE=$(cd "$LINK" 2>/dev/null && pwd -P) || {
    echo "ERROR: $LINK exists but is not a directory (or is unreadable)." >&2
    exit 1
}

if [ ! -d "$WORKSPACE/all/common" ]; then
    echo "ERROR: $WORKSPACE doesn't look like a NextUI workspace (missing all/common)." >&2
    exit 1
fi

if [ ! -d "$WORKSPACE/$PLATFORM/libmsettings" ]; then
    echo "ERROR: $WORKSPACE doesn't have $PLATFORM/libmsettings (missing platform checkout)." >&2
    exit 1
fi

exec docker run -it --rm \
    -v "$WORKSPACE/all":/root/workspace/all \
    -v "$WORKSPACE/$PLATFORM":/root/workspace/$PLATFORM \
    -v "$HERE":/root/workspace/nextui-music-player \
    "ghcr.io/loveretro/${PLATFORM}-toolchain" "$@"
