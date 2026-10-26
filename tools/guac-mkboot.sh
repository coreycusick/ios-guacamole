#!/bin/bash
# Build a OnePlus 7 Pro (guacamole) boot image: CI kernel + ADT + shim.
# Runs on macOS/Linux with python3 and gh; nothing is written to the phone.
#
#   tools/guac-mkboot.sh RUN_ID STOCK_boot.img SHIM.Image OUT.img ["extra boot-args"]
#
# RUN_ID: the `xnu` Actions run of this fork (gh run list -w xnu).
# The experimental Wi-Fi branch enables /dev/pmem, /dev/wlan and the wifid
# device-tree node by default (WIFI=0 disables them). The user must still
# supply iOS root boot-args in EXTRA and install/start the wland userspace
# tool for radio initialization. Always test with `fastboot boot`.
# CPUS=N (default 8) sets cpus=; all OnePlus CPUs are enabled by default.
# GPU_STAGE=0..11 defaults to 0. GL_UI=0/1 defaults to 0, keeping
# CoreAnimation's known-good software renderer while Wi-Fi is tested.
# GPU_STAGE=11 GL_UI=0 tests the GPU without enabling the black-screen-prone
# accelerated UI; GL_UI=1 explicitly tests experimental G4 presentation.
# KEYS=0/1 (default 1, PM8150 keys from 0084), TOUCH=0/1 and USB=0/1/2
# (default 0: still sweet hardware) set sweetkeys/sweettouch/sweetusb.
set -euo pipefail
[ $# -ge 4 ] || { sed -n '2,11p' "$0"; exit 1; }
# Resolve paths against the caller's directory BEFORE changing into the repo.
abs() { case "$1" in /*) echo "$1" ;; *) echo "$PWD/$1" ;; esac; }
RUN=$1 STOCK=$(abs "$2") SHIM=$(abs "$3") OUT=$(abs "$4") EXTRA=${5:-}
[ -f "$STOCK" ] || { echo "stock boot image not found: $STOCK"; exit 1; }
[ -f "$SHIM" ] || { echo "shim Image not found: $SHIM"; exit 1; }
GPU_STAGE=${GPU_STAGE:-0}
GL_UI=${GL_UI:-0}
case "$GPU_STAGE" in 0|1|2|3|4|5|6|7|8|9|10|11) ;; *) echo "GPU_STAGE must be 0..11" >&2; exit 1 ;; esac
case "$GL_UI" in 0|1) ;; *) echo "GL_UI must be 0 or 1" >&2; exit 1 ;; esac
cd "$(dirname "$0")/.."
K=xnu/out-$RUN/kernel.development.sweet
if [ ! -f "$K" ]; then
	gh run download "$RUN" -n xnu-kernel -D "xnu/out-$RUN"
fi
[ -f "$K" ] || { echo "no $K in the artifact"; ls "xnu/out-$RUN"; exit 1; }
mkdir -p shim/out
ADT_OPTS=(--highmem --guacamole)
WIFI_BOOTARGS='sweetpmem=0 sweetwifid=0 sweetwifi=0'
if [ "${WIFI:-1}" = 1 ]; then
	ADT_OPTS+=(--guacamole-wifi)
	WIFI_BOOTARGS='sweetpmem=1 sweetwifid=1 sweetwifi=1'
fi
python3 tools/mkadt.py shim/out/guac-hi.adt "${ADT_OPTS[@]}"
ARGS="debug=0xc serial=3 cpus=${CPUS:-8} keepsyms=1 -enable_kprintf_spam -noprogress sweettouch=${TOUCH:-0} sweetkeys=${KEYS:-1} sweetusb=${USB:-0} sweetgpu=$GPU_STAGE sweetgl=$GL_UI $WIFI_BOOTARGS wdt=-1 $EXTRA"
python3 tools/mkpayload.py "$K" shim/out/guac-hi.adt shim/out/payload-guac.bin --cmdline "$ARGS"
python3 tools/mkguacboot.py "$STOCK" "$SHIM" "$OUT" --ramdisk shim/out/payload-guac.bin
echo "boot-args: $ARGS"
echo "next: fastboot boot $OUT"
