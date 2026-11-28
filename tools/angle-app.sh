#!/bin/bash
# OpenGL ES on ANGLE for one app (2026-11-27): add /usr/lib/libsweet-glapp.dylib (userland/gpu: eagl.c + gl_angle.c,
# ANGLE on Vulkan / Turnip) to the app's executable on the phone, re-signed ad hoc with the executable's own
# entitlements. The original stays as EXECUTABLE.pre-angle next to it. Run on the Mac; the phone is `ssh guac`.
#   tools/angle-app.sh /Applications/Name.app            add ANGLE
#   tools/angle-app.sh /Applications/Name.app --undo     put the original back
#   EXTRA_DYLIBS="/usr/lib/libX.dylib ..." tools/angle-app.sh ...   also load these (e.g. libsweet-skquiet.dylib)
#   ENTS=ents.plist tools/angle-app.sh ...                sign with these entitlements instead (an app installed without
#                                                         its IPA's, e.g. iCloud: CloudKit traps in CKContainer)
# ios-sweet, MIT.
set -euo pipefail
APP=${1:?usage: angle-app.sh /Applications/Name.app [--undo]}
TOOLS=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d /tmp/angle-app.XXXXXX)
trap 'rm -rf "$W"' EXIT

ssh guac "cat \"$APP/Info.plist\"" > "$W/Info.plist"
EXE=$(plutil -extract CFBundleExecutable raw "$W/Info.plist")
echo "app $APP, executable $EXE"

if [ "${2:-}" = "--undo" ]; then
	ssh guac "cd \"$APP\" && [ -f \"$EXE.pre-angle\" ] && mv \"$EXE.pre-angle\" \"$EXE\" && echo restored"
	exit 0
fi

# start from the original if ANGLE was added before
ssh guac "cd \"$APP\" && if [ -f \"$EXE.pre-angle\" ]; then cat \"$EXE.pre-angle\"; else cat \"$EXE\"; fi" > "$W/$EXE"
if [ -n "${ENTS:-}" ]; then cp "$ENTS" "$W/ents.plist"; else codesign -d --entitlements :- "$W/$EXE" > "$W/ents.plist" 2>/dev/null || true; fi
python3 "$TOOLS/insert-dylib.py" "$W/$EXE" /usr/lib/libsweet-glapp.dylib
for extra in ${EXTRA_DYLIBS:-}; do python3 "$TOOLS/insert-dylib.py" "$W/$EXE" "$extra"; done
if [ -s "$W/ents.plist" ]; then
	codesign -f -s - --entitlements "$W/ents.plist" "$W/$EXE"
else
	codesign -f -s - "$W/$EXE"
fi
ssh guac "cat > \"$APP/$EXE.new\"" < "$W/$EXE"
ssh guac "cd \"$APP\" && { [ -f \"$EXE.pre-angle\" ] || cp -p \"$EXE\" \"$EXE.pre-angle\"; } && mv \"$EXE.new\" \"$EXE\" && chmod 755 \"$EXE\" && ls -l \"$EXE\" \"$EXE.pre-angle\""
echo "done: open the app; undo with: tools/angle-app.sh \"$APP\" --undo"
