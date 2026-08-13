#!/bin/sh
# Cross-compile the Music Player in the Docker toolchain, then push the binary
# to a connected device over ADB. Run from anywhere.
export PLATFORM=tg5040
cd "$(dirname "$0")" || exit
sh run-docker.sh /bin/sh -c "cd /root/workspace/$PLATFORM/libmsettings && make build CROSS_COMPILE=aarch64-nextui-linux-gnu- PREFIX=/opt/nextui PREFIX_LOCAL=/opt/nextui && cd /root/workspace/nextui-music-player/src && make PLATFORM=$PLATFORM" || exit
adb push "bin/$PLATFORM/musicplayer.elf" "/mnt/SDCARD/Tools/$PLATFORM/Music Player.pak/bin/$PLATFORM/musicplayer.elf"
