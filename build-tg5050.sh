#!/bin/sh
# Cross-compile the Music Player for tg5050 in the Docker toolchain, then push
# the binary to a connected device over ADB. Run from anywhere.
cd "$(dirname "$0")" || exit
PLATFORM=tg5050 sh run-docker.sh /bin/sh -c 'cd nextui-music-player/src && make PLATFORM=tg5050' || exit
adb push bin/tg5050/musicplayer.elf "/tmp/m/bin/tg5050/musicplayer.elf"
