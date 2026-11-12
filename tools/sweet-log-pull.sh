#!/bin/sh
# Read the SWEET console log (kernel patch 0047) from a phone booted into
# OrangeFox recovery (fastboot boot recovery/recovery.img; adb root shell).
#   tools/sweet-log-pull.sh [-s SERIAL] OUT.txt
# Only reads: the last 1025 blocks (4 KiB) of the iOS root partition (ios; ios16 or userdata on other layouts).
set -eu

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
	echo "Usage: $(basename "$0") [-s SERIAL] OUT.txt"
	echo "Pull the 4 MiB kernel log ring from the iOS root partition (ios, ios16 or userdata; PART=name) via recovery ADB."
	exit 0
fi

ADB="adb"
if [ "${1:-}" = "-s" ]; then
	shift
	SERIAL=${1:?Error: -s requires a device serial}
	shift
	ADB="adb -s $SERIAL"
fi

out=${1:?usage: sweet-log-pull.sh [-s SERIAL] OUT.txt}
command -v adb >/dev/null 2>&1 || { echo "Error: adb not found in PATH" >&2; exit 1; }

# The ring sits at the end of the iOS root partition: PART=name (default: the first of ios, ios16, userdata that exists).
parts=${PART:-ios ios16 userdata}
dev=$($ADB shell "for p in $parts; do for d in /dev/block/by-name/\$p /dev/block/bootdevice/by-name/\$p; do [ -e \$d ] && readlink -f \$d && exit 0; done; done" 2>/dev/null | tr -d '\r')
[ -n "$dev" ] || { echo "no partition named $parts on the connected device" >&2; exit 1; }
bytes=$($ADB shell blockdev --getsize64 "$dev" | tr -d '\r')
skip=$((bytes / 4096 - 1025))
$ADB exec-out "dd if=$dev bs=4096 skip=$skip count=1025 2>/dev/null" > "$out.bin"
echo "$dev: $bytes bytes, read blocks $skip.. ($(stat -c %s "$out.bin") bytes)" >&2
python3 "$(dirname "$0")/sweet-log.py" "$out.bin" "$out"
