#!/bin/sh
# Build the Pixel 4-derived radio userland with the iOS 16 SDK for guacamole.
# The GM1917 experimental modem/WLAN initialization is enabled in guac_board.h.
set -eu
cd "$(dirname "$0")"
mkdir -p out
xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=16.0 -O2 -Wall -Wextra \
    -Wno-unused-parameter -o out/wland \
    wland.c smem.c pmem.c glink.c link.c qmi.c rmtfs.c pdmapper.c \
    tftp.c scm.c wlfw.c radio.c ipnet.c bridge.c
codesign -f -s - out/wland
ls -lh out/wland
