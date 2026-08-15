#!/bin/sh
DIR="$(dirname "$0")"
cd "$DIR"

# Use system PLATFORM variable, fallback to tg5040 if not set
[ -z "$PLATFORM" ] && PLATFORM="tg5040"

export LD_LIBRARY_PATH="$DIR:$DIR/bin:$DIR/bin/$PLATFORM:$LD_LIBRARY_PATH:/usr/bin"

CPU_FREQ=/sys/devices/system/cpu/cpu0/cpufreq

# Save current CPU scaling state and restore on exit (including crash)
PREV_GOVERNOR=$(cat "$CPU_FREQ/scaling_governor")
PREV_MIN_FREQ=$(cat "$CPU_FREQ/scaling_min_freq")
PREV_MAX_FREQ=$(cat "$CPU_FREQ/scaling_max_freq")

restore_cpu() {
	echo "$PREV_GOVERNOR" > "$CPU_FREQ/scaling_governor"
	echo "$PREV_MIN_FREQ" > "$CPU_FREQ/scaling_min_freq"
	echo "$PREV_MAX_FREQ" > "$CPU_FREQ/scaling_max_freq"
}
trap restore_cpu EXIT

echo conservative > "$CPU_FREQ/scaling_governor"
cat "$CPU_FREQ/cpuinfo_min_freq" > "$CPU_FREQ/scaling_min_freq"
cat "$CPU_FREQ/cpuinfo_max_freq" > "$CPU_FREQ/scaling_max_freq"

# Development hook:
# - if the restart flag file is present when app exits, restart the app.
RESTART_FLAG_FILE=/tmp/nextui-music-player.restart

while :; do
	rm -f "$RESTART_FLAG_FILE"
	"$DIR/bin/$PLATFORM/musicplayer.elf" > "$LOGS_PATH/music-player.txt" 2>&1

	[ -e "$RESTART_FLAG_FILE" ] || break
done
