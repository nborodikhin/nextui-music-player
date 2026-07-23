#!/bin/sh
# Cross-compile the Music Player for tg5040 in the Docker toolchain, then push
# the binary to a connected device over ADB. Run from anywhere.
cd "$(dirname "$0")" || exit
PLATFORM=tg5040 sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5040' || exit
adb push bin/tg5040/musicplayer.elf "/tmp/m/bin/tg5040/musicplayer.elf"
