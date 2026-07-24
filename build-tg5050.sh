#!/bin/sh
# Cross-compile the Music Player in the Docker toolchain, then push the binary
# to a connected device over ADB. Run from anywhere.
export PLATFORM=tg5050
cd "$(dirname "$0")" || exit
sh run-docker.sh /bin/sh -c "cd nextui-music-player/src && make PLATFORM=$PLATFORM" || exit
adb push "bin/$PLATFORM/musicplayer.elf" "/mnt/SDCARD/Tools/$PLATFORM/Music Player.pak/bin/$PLATFORM/musicplayer.elf"
