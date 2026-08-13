#!/bin/sh
# Full build + package + deploy: cross-compiles the requested platform(s),
# assembles the complete pak via create_dist.py, and pushes the whole folder
# to the SD card so on-device res/state/stations/pak.json never drift from
# source. Slower than dev.sh (which only refreshes the binary) -- use this
# after touching anything besides src/*.c, or before handing a build to
# someone else to test.
#
# Usage: sh deploy.sh [tg5040|tg5050|all]
set -e
TARGET=${1:-all}
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit

case "$TARGET" in
    tg5040) PLATFORMS="tg5040" ;;
    tg5050) PLATFORMS="tg5050" ;;
    all)    PLATFORMS="tg5040 tg5050" ;;
    *) echo "usage: deploy.sh [tg5040|tg5050|all]" >&2; exit 1 ;;
esac

for PLATFORM in $PLATFORMS; do
    echo "== Building $PLATFORM =="
    if [ "$PLATFORM" = "tg5050" ]; then
        # tg5050 toolchain ships no libfdk-aac; stage the prebuilt copy where
        # the linker looks for it (see build-tg5050.sh for why this runs on
        # the host rather than inside the container).
        mkdir -p "src/build/$PLATFORM" || exit
        cp "bin/$PLATFORM"/libfdk-aac.so* "src/build/$PLATFORM/" || exit
    fi
    PLATFORM=$PLATFORM sh run-docker.sh /bin/sh -c "cd /root/workspace/$PLATFORM/libmsettings && make build CROSS_COMPILE=aarch64-nextui-linux-gnu- PREFIX=/opt/nextui PREFIX_LOCAL=/opt/nextui && cd /root/workspace/nextui-music-player/src && make PLATFORM=$PLATFORM" || exit
done

python3 create_dist.py --platforms "$(echo "$PLATFORMS" | tr ' ' ',')"

for PLATFORM in $PLATFORMS; do
    echo "== Pushing to /mnt/SDCARD/Tools/$PLATFORM/ =="
    adb push "dist/Music Player.pak" "/mnt/SDCARD/Tools/$PLATFORM/"
done

echo "Deployed: $PLATFORMS"
