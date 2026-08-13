#!/bin/sh
# Quick iterate loop against a connected device: build, push just the binary
# (via build-<platform>.sh), kill + relaunch the app on-device, then tail its
# log. Assumes the full pak is already installed on the SD card -- run
# deploy.sh at least once first. This only ever refreshes musicplayer.elf.
#
# Usage: sh dev.sh [tg5040|tg5050]
set -e
PLATFORM=${1:-tg5040}
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit

case "$PLATFORM" in
    tg5040|tg5050) ;;
    *) echo "usage: dev.sh [tg5040|tg5050]" >&2; exit 1 ;;
esac

sh "build-$PLATFORM.sh"

PAK_DIR="/mnt/SDCARD/Tools/$PLATFORM/Music Player.pak"

echo "Relaunching on device..."
adb shell "pkill -f musicplayer.elf" 2>/dev/null || true
# Re-exec launch.sh directly (bypassing the NextUI menu) so the fresh binary
# is picked up immediately -- same entry point NextUI uses when you open the
# pak from the menu.
adb shell "cd '$PAK_DIR' && PLATFORM=$PLATFORM nohup ./launch.sh >/tmp/music-player-dev.log 2>&1 &"

LOG=$(adb shell "find /mnt/SDCARD/.userdata -maxdepth 4 -name music-player.txt 2>/dev/null | head -1" | tr -d '\r')
if [ -n "$LOG" ]; then
    echo "Tailing $LOG (Ctrl-C stops watching; the app keeps running)"
    adb shell "tail -f '$LOG'"
else
    echo "App relaunched, but couldn't find music-player.txt to tail (check /tmp/music-player-dev.log on device)."
fi
