#!/bin/sh
# Cross-compile the Music Player in the Docker toolchain, then push the binary
# to a connected device over ADB. Run from anywhere.
export PLATFORM=tg5050
cd "$(dirname "$0")" || exit
# The tg5050 toolchain image ships no libfdk-aac, so stage the prebuilt copy
# bundled in bin/tg5050 where the linker will find it: the Makefile puts
# -L$(BUILD_DIR) first, and that directory is inside the mounted workspace, so
# the copy is done here on the host rather than inside the container.
mkdir -p "src/build/$PLATFORM" || exit
cp bin/$PLATFORM/libfdk-aac.so* "src/build/$PLATFORM/" || exit
sh run-docker.sh /bin/sh -c "cd nextui-music-player/src && make PLATFORM=$PLATFORM" || exit
adb push "bin/$PLATFORM/musicplayer.elf" "/mnt/SDCARD/Tools/$PLATFORM/Music Player.pak/bin/$PLATFORM/musicplayer.elf"
