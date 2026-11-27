#!/bin/bash
# Build the iOS 27.2 (24B119, iPhone15,2) root image for the `ios26` partition on macOS (hdiutil + bsdtar), from the
# tars and overlays in research/ios27.2 (NOTES.md "iOS 27.2 arm64e userland lowering"). Derived from
# tools/mkios26root-mac.sh, plus what was patched on the phone by hand for iOS 26 (NOTES 2026-11-13): launchd's
# Boot.fips task gets -N (tools/launchd-fips-noverify.py), job com.apple.fairplayd.H2 leaves launchd.plist, and the
# DriverKit cache overlay. Same layout as tools/mkiosroot.sh makes for iOS 16:
# case-sensitive journaled HFS+, OS volume at /, SystemOS cryptex at /private/preboot/Cryptexes/OS,
# AppOS at .../App, the shared cache and cryptex-only files hard-linked to their root paths.
#
#   tools/mkios27root-mac.sh [SIZE]        (SIZE default 30g)  -> research/ios27.2/ios27-root.cdr (raw)
#
# Needs sudo (ownership). Writes nothing to the phone.
set -euo pipefail
TOOLS="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$0")/../research/ios27.2"
size=${1:-30g}
R=/tmp/ios27-root
for f in os-root.tar systemos-cryptex.tar appos-cryptex.tar overlay/os-root-lowered.tar \
	overlay/systemos-cryptex-lowered.tar overlay/appos-cryptex-lowered.tar overlay/dsc-lowered-0.tar \
	overlay/driverkit-dsc-lowered.tar; do
	[ -f "$f" ] || { echo "missing research/ios27.2/$f" >&2; exit 1; }
done
[ ! -e ios27-root.sparseimage ] || { echo "ios27-root.sparseimage exists: remove it first" >&2; exit 1; }

hdiutil create -size "$size" -type SPARSE -fs "Case-sensitive Journaled HFS+" -layout NONE -volname iOSRoot27 ios27-root
hdiutil attach -owners on -nobrowse -mountpoint "$R" ios27-root.sparseimage
trap 'hdiutil detach "$R" >/dev/null 2>&1 || true' EXIT

x() {	# extract tar $1 into $2 as root, keeping owners and modes, without macOS AppleDouble files
	echo "extract $1 -> ${2#"$R"}/"
	sudo mkdir -p "$2"
	sudo tar -xpf "$1" -C "$2" --exclude '._*' --exclude './._*'
}
x os-root.tar "$R"
x overlay/os-root-lowered.tar "$R"
x systemos-cryptex.tar "$R/private/preboot/Cryptexes/OS"
x overlay/systemos-cryptex-lowered.tar "$R/private/preboot/Cryptexes/OS"
x overlay/driverkit-dsc-lowered.tar "$R/private/preboot/Cryptexes/OS"
for t in overlay/dsc-lowered-*.tar; do x "$t" "$R/private/preboot/Cryptexes/OS"; done
x appos-cryptex.tar "$R/private/preboot/Cryptexes/App"
x overlay/appos-cryptex-lowered.tar "$R/private/preboot/Cryptexes/App"

# Shared cache where dyld looks without cryptex mounts: hard links (no extra space).
dsc=private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld
sudo mkdir -p "$R/System/Library/Caches/com.apple.dyld"
for f in "$R/$dsc"/*; do
	sudo ln -f "$f" "$R/System/Library/Caches/com.apple.dyld/${f##*/}"
done
# Cryptex files with no counterpart in the OS root: hard-linked (symlinks copied) to their root paths.
for c in OS App; do
	(cd "$R/private/preboot/Cryptexes/$c" && sudo find . ! -type d ! -path './System/Library/Caches/com.apple.dyld/*') |
	while IFS= read -r f; do
		f=${f#./}
		if ! sudo test -e "$R/$f" && ! sudo test -L "$R/$f"; then
			sudo mkdir -p "$R/$(dirname "$f")"
			src="$R/private/preboot/Cryptexes/$c/$f"
			if sudo test -L "$src"; then sudo cp -P "$src" "$R/$f"; else sudo ln "$src" "$R/$f"; fi
		fi
	done
done

# Removable system apps (Safari, Camera, Photos, ...): 27.2's installd stages them through MIStagingManager, which
# refuses any volume that is not APFS ("Expected install path file:///private/var/ to be located on an APFS formatted
# volume; got hfs", mobile_installation_helper.log, 2026-11-14) and the staged copies are then discarded. Put them in
# /Applications instead: LaunchServices registers /Applications apps as system apps, no installd involved.
st="$R/private/var/staged_system_apps"
if sudo test -d "$st"; then
	n=0
	for a in $(cd "$st" && sudo find . -maxdepth 1 -name '*.app' | sed 's#^\./##'); do
		if sudo test -e "$R/Applications/$a"; then
			echo "staged app $a: /Applications/$a exists, left in place"
		else
			sudo mv "$st/$a" "$R/Applications/$a"; n=$((n + 1))
		fi
	done
	echo "staged system apps moved to /Applications: $n"
fi

# launchd: Boot.fips -N (lowered corecrypto fails cc_fips_test's integrity HMAC; POST tests still run).
sudo python3 "$TOOLS/launchd-fips-noverify.py" "$R/sbin/launchd" /tmp/ios27-launchd.new --no-reboot-task sysstatuscheck
sudo sh -c 'cat /tmp/ios27-launchd.new > "$1"' _ "$R/sbin/launchd"	# keeps owner, mode, hard links
sudo rm -f /tmp/ios27-launchd.new
# BlueTool: alias the BCM4357B1/USI legacy row (whose HCD is built into BlueTool) to D73's ProductHash, so the
# btbridge-emulated BCM4357B1 finds a row. Without it: "HCD file corrupt!", then "no firmware for this platform",
# and Bluetooth restarts forever. Page hashes are refreshed by the script; the signature and entitlements stay.
sudo python3 "$TOOLS/patch-bluetool27-profile.py" "$R/usr/sbin/BlueTool" /tmp/ios27-BlueTool.new
sudo sh -c 'cat /tmp/ios27-BlueTool.new > "$1"' _ "$R/usr/sbin/BlueTool"	# keeps owner, mode, hard links
sudo rm -f /tmp/ios27-BlueTool.new
# powerd: this phone has no Apple battery authentication; take powerd's own "not supported" path (legacy devices
# without auth) instead of waiting forever for an auth service, which left Settings' Battery Health spinning.
# One branch (tools/patch-powerd-battauth.py); page hashes refreshed, signature and entitlements stay. 2026-11-26.
PD="$R/System/Library/CoreServices/powerd.bundle/powerd"
sudo python3 "$TOOLS/patch-powerd-battauth.py" "$PD" /tmp/ios27-powerd.new
sudo sh -c 'cat /tmp/ios27-powerd.new > "$1"' _ "$PD"	# keeps owner, mode, hard links
sudo rm -f /tmp/ios27-powerd.new
# Settings' Battery pane asks the same battery-auth question and shows an auth spinner forever; same treatment
# (tools/patch-bui-battauth.py: its two MGGetBoolAnswer calls answer false). 2026-11-27.
BUI="$R/System/Library/PreferenceBundles/BatteryUsageUI.bundle/BatteryUsageUI"
sudo python3 "$TOOLS/patch-bui-battauth.py" "$BUI" /tmp/ios27-BUI.new
sudo sh -c 'cat /tmp/ios27-BUI.new > "$1"' _ "$BUI"	# keeps owner, mode, hard links
sudo rm -f /tmp/ios27-BUI.new
# wifid (WiFiPolicy is built in): its auto-join RSSI threshold for this phone is above any real signal, so a known
# network at -47 dBm was refused ("Below minimum RSSI threshold") and Wi-Fi never joined by itself. One branch
# (tools/patch-wifid-ajrssi.py: b.lt -> nop); the scan's own -80 dBm minimum still applies. 2026-11-27.
WD="$R/usr/sbin/wifid"
sudo python3 "$TOOLS/patch-wifid-ajrssi.py" "$WD" /tmp/ios27-wifid.new
sudo sh -c 'cat /tmp/ios27-wifid.new > "$1"' _ "$WD"	# keeps owner, mode, hard links
sudo rm -f /tmp/ios27-wifid.new
# launchd.plist: no FairPlay here; com.apple.fairplayd.H2 exits -42306 and KeepAlive restarts it forever.
# 2026-11-19: com.apple.cameracaptured goes too: the synthetic camera kept restarting it (about 400 MB of GPU pool,
# then a jetsam kill, every few seconds), which resprung SpringBoard when leaving a game. backboardd gets
# MallocDeferredReclaim=1 MallocLargeCache=1: its frame buffers no longer go back to the kernel (madvise) every frame.
sudo python3 - "$R/System/Library/xpc/launchd.plist" <<'PYEOF'
import plistlib, sys
p = sys.argv[1]
d = plistlib.load(open(p, 'rb'))
# 2026-11-20: com.apple.PerfPowerServices too: it loops forever over SMC keys (no AppleSMC here), a core at 100 %.
gone = [k for k, j in d['LaunchDaemons'].items() if j.get('Label') in ('com.apple.fairplayd.H2', 'com.apple.cameracaptured',
                                                                   'com.apple.PerfPowerServices')]
for k in gone:
    del d['LaunchDaemons'][k]
for j in d['LaunchDaemons'].values():
    if j.get('Label') == 'com.apple.backboardd':
        j.setdefault('EnvironmentVariables', {}).update(MallocDeferredReclaim='1', MallocLargeCache='1')
data = plistlib.dumps(d, fmt=plistlib.FMT_BINARY)
with open(p, 'r+b') as f:	# rewrite in place: owner and mode stay
    f.write(data)
    f.truncate()
print('launchd.plist: removed', gone)
PYEOF

# 2026-11-20: files the old phone root was given by hand (boxes, SSH), all kept in tools/ios27root/:
#  - no Setup: purplebuddy + purplebuddy.notbackedup (SetupDone, SetupFinishedAllSteps, ForceNoBuddy, UserChoseLanguage,
#    SetupVersion/TVSetupVersion 99, every "<Pane>Presented" key in 27.2's Setup binary);
#  - CoreAnimation: no deep / compressed framebuffers (else backboardd makes '&BGA' surfaces and colours go wrong,
#    NOTES 2026-11-14 18:15): com.apple.backboardd (backboardd runs as mobile) and .GlobalPreferences, + en-US;
#  - Wi-Fi: SystemConfiguration preferences.plist with a Wi-Fi service on en0 in set "Automatic" (27.2 does not create
#    one; without it configd never configures en0 and there is no IP, NOTES 2026-11-14 20:45);
#  - GPU: NativeAdrenoMetal.primelist (the 310 .metallibs to prime; else backboardd walks the whole disk, 2026-11-17).
F="$TOOLS/ios27root"
pref="$R/private/var/mobile/Library/Preferences"
sudo install -d -o 501 -g 501 -m 0755 "$pref"
for f in com.apple.purplebuddy.plist com.apple.purplebuddy.notbackedup.plist com.apple.backboardd.plist .GlobalPreferences.plist; do
	sudo install -o 501 -g 501 -m 0600 "$F/$f" "$pref/$f"
done
echo "Setup skipped: purplebuddy prefs installed; CoreAnimation framebuffer prefs installed"
sc="$R/private/var/preferences/SystemConfiguration"
sudo install -d -o 0 -g 0 -m 0755 "$sc"
sudo install -o 0 -g 0 -m 0644 "$F/preferences.plist" "$sc/preferences.plist"
echo "Wi-Fi: network service for en0 installed"
sudo install -o 0 -g 0 -m 0644 "$F/NativeAdrenoMetal.primelist" "$R/System/Library/Extensions/NativeAdrenoMetal.primelist"
echo "GPU: shader priming list installed"

# Network extras (all optional; local files only, never in git: research/ is ignored).
#   research/ios27.2/extras/bootstrap-ssh-iphoneos-arm64.tar.zst   Procursus rootless bootstrap with OpenSSH
#       (https://apt.procurs.us/bootstraps/1900/) -> /var/jb; host keys made here; launchd job com.sweet.sshd
#   research/ios27.2/extras/ssh_host_keys/                         sshd host keys, reused by every root (made on first build)
#   research/ios27.2/extras/authorized_keys                         -> /var/root/.ssh and /var/mobile/.ssh
#   research/ios27.2/extras/fw/{modem,rmtfs}/...                    -> /var/local/fw (userland/wland/README.md)
#   research/ios27.2/extras/wlan.conf (ssid=, psk=)                 -> /var/local/wlan.conf (0600)
#   research/ios27.2/extras/fw/gpu/*                                -> /usr/standalone/firmware/sweet
#   research/ios27.2/extras/fw/modem-all/adsp.*                     -> /var/local/fw/adsp
#   research/ios27.2/extras/fw/tfa98xx.cnt                          -> /var/local/fw/tfa98xx.cnt
#   userland/out/wland, userland/out/netsvc                         -> /var/local/bin; launchd job com.sweet.wland
#   userland/out/sweet-displayd                                      -> /var/local/bin; launchd job com.sweet.displayd
#   userland/out/sweet-gnssfeed                                      -> /var/local/bin; launchd job com.sweet.gnssfeed
#   userland/out/sweet-bars                                          -> /var/local/bin; launchd job com.sweet.bars
#   userland/out/HakiosAudio.driver                                  -> system HAL plugin; ADSP job only if TFA container exists
X="$PWD/extras"
U="$TOOLS/../userland/out"
jobs=()
if [ -d "$X/fw/gpu" ]; then
	sudo install -d -m 0755 "$R/usr/standalone/firmware/sweet"
	sudo cp -R "$X/fw/gpu/." "$R/usr/standalone/firmware/sweet/"
	sudo chown -R 0:0 "$R/usr/standalone/firmware/sweet"
	sudo find "$R/usr/standalone/firmware/sweet" -type f -exec chmod 0644 {} \;
	echo "GPU firmware: $(cd "$X/fw/gpu" && find . -type f | wc -l | tr -d ' ') files -> /usr/standalone/firmware/sweet"
fi
bs="$X/bootstrap-ssh-iphoneos-arm64.tar.zst"
if [ -f "$bs" ]; then
	echo "extract $bs -> /private/var/jb"
	# rootless entries are ./var/jb/...; /var is a symlink on the image, so rewrite to ./private/var/...
	sudo tar -xpf "$bs" -C "$R" -s '#^\./var/#./private/var/#' -s '#^var/#private/var/#' --exclude '._*'
	sudo test -x "$R/private/var/jb/usr/sbin/sshd" || { echo "bootstrap: no /var/jb/usr/sbin/sshd" >&2; exit 1; }
	sudo mkdir -p "$R/private/var/jb/etc/ssh"
	# 2026-11-20: the same host keys in every root (kept in the git-ignored research/ios27.2/extras/ssh_host_keys/),
	# so a reflash no longer trips "REMOTE HOST IDENTIFICATION HAS CHANGED" on the Mac. Made once, then reused.
	hk="$X/ssh_host_keys"
	if ls "$hk"/ssh_host_*_key >/dev/null 2>&1; then
		sudo cp "$hk"/ssh_host_* "$R/private/var/jb/etc/ssh/"
		echo "ssh host keys: reused from $hk"
	else
		sudo ssh-keygen -A -f "$R/private/var/jb"
		mkdir -p "$hk"
		sudo cp "$R/private/var/jb/etc/ssh/"ssh_host_* "$hk/"
		sudo chown "$(id -u):$(id -g)" "$hk"/ssh_host_*
		chmod 0600 "$hk"/ssh_host_*_key
		echo "ssh host keys: made and saved to $hk"
	fi
	sudo chown 0:0 "$R/private/var/jb/etc/ssh/"ssh_host_*
	sudo chmod 0600 "$R/private/var/jb/etc/ssh/"ssh_host_*_key
	sudo chmod 0644 "$R/private/var/jb/etc/ssh/"ssh_host_*.pub
	# Procursus ships root locked ("!"); give root the same password as mobile ("alpine") so ssh root@ works.
	mp="$R/private/var/jb/etc/master.passwd"
	mh=$(sudo awk -F: '$1=="mobile"{print $2}' "$mp")
	[ -n "$mh" ] && sudo awk -F: -v OFS=: -v h="$mh" '$1=="root" && $2=="!"{$2=h}1' "$mp" | sudo tee "$mp.new" >/dev/null &&
		sudo chmod 0600 "$mp.new" && sudo mv "$mp.new" "$mp" && echo "root password set (alpine)"
	# login shells in /etc/master.passwd are /bin/sh etc., which iOS does not ship
	for sh in sh bash zsh; do
		if ! sudo test -e "$R/bin/$sh" && ! sudo test -L "$R/bin/$sh"; then
			for t in "/var/jb/bin/$sh" "/var/jb/usr/bin/$sh"; do
				if sudo test -e "$R/private$t" || sudo test -L "$R/private$t"; then
					sudo ln -s "$t" "$R/bin/$sh"; echo "/bin/$sh -> $t"; break
				fi
			done
		fi
	done
	if [ -f "$X/authorized_keys" ]; then
		# Procursus rootless has its own /var/jb/etc/master.passwd: homes /var/jb/var/root and /var/jb/var/mobile
		# (root's password is locked "!", mobile's is "alpine"). Keys go there; the system homes too, harmless.
		for h in root:0:0 mobile:501:501; do
			IFS=: read -r who uid gid <<<"$h"
			for home in "$R/private/var/jb/var/$who" "$R/private/var/$who"; do
				sudo install -d -o "$uid" -g "$gid" -m 0755 "$home"
				sudo install -d -o "$uid" -g "$gid" -m 0700 "$home/.ssh"
				sudo install -o "$uid" -g "$gid" -m 0600 "$X/authorized_keys" "$home/.ssh/authorized_keys"
			done
		done
		echo "authorized_keys installed for root and mobile (/var/jb/var/* and /var/*)"
	fi
	jobs+=(sshd pwdmkdb)
fi
if [ -f "$U/wland" ] && [ -f "$U/netsvc" ]; then
	sudo install -d -m 0755 "$R/private/var/local/bin"
	sudo install -m 0755 "$U/wland" "$U/netsvc" "$R/private/var/local/bin/"
	if [ -d "$X/fw" ]; then
		sudo mkdir -p "$R/private/var/local/fw"
		sudo cp -R "$X/fw/." "$R/private/var/local/fw/"
		sudo chown -R 0:0 "$R/private/var/local/fw"
		echo "firmware: $(cd "$X/fw" && find . -type f | wc -l | tr -d ' ') files -> /var/local/fw"
	else
		echo "WARNING: no extras/fw: wland cannot boot the modem" >&2
	fi
	if [ -f "$X/wlan.conf" ]; then
		sudo install -o 0 -g 0 -m 0600 "$X/wlan.conf" "$R/private/var/local/wlan.conf"
	fi
	jobs+=(wland)
fi
if [ -f "$U/sweet-displayd" ]; then
	sudo install -d -m 0755 "$R/private/var/local/bin"
	sudo install -m 0755 "$U/sweet-displayd" "$R/private/var/local/bin/sweet-displayd"
	jobs+=(displayd)
	echo "sweet-displayd: installed signed userland daemon"
fi
if [ -f "$U/sweet-gnssfeed" ]; then
	sudo install -d -m 0755 "$R/private/var/local/bin"
	sudo install -m 0755 "$U/sweet-gnssfeed" "$R/private/var/local/bin/sweet-gnssfeed"
	jobs+=(gnssfeed)
	echo "sweet-gnssfeed: installed; boot job enabled (GPS needs /var/local/gnss.on, see userland/wland/gnss.c)"
fi
if [ -f "$U/sweet-bars" ]; then
	sudo install -d -m 0755 "$R/private/var/local/bin"
	sudo install -m 0755 "$U/sweet-bars" "$R/private/var/local/bin/sweet-bars"
	jobs+=(bars)
	echo "sweet-bars: installed; boot job enabled (bars from /var/local/bars.conf)"
fi
if [ -d "$U/HakiosAudio.driver" ]; then
	plugin="$R/System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver"
	sudo install -d -m 0755 "$plugin"
	sudo cp -R "$U/HakiosAudio.driver/." "$plugin/"
	sudo chown -R 0:0 "$plugin"
	echo "HakiosAudio.driver: installed HAL plugin"
fi
if [ -f "$U/adspd" ] && [ -f "$U/tfa" ] && [ -f "$X/fw/modem-all/adsp.mdt" ]; then
	local="$R/private/var/local"
	sudo install -d -m 0755 "$local/bin" "$local/fw/adsp"
	sudo install -m 0755 "$U/adspd" "$U/tfa" "$local/bin/"
	for f in "$X"/fw/modem-all/adsp.*; do
		[ -f "$f" ] && sudo install -o 0 -g 0 -m 0644 "$f" "$local/fw/adsp/"
	done
	if [ -f "$X/fw/modem-all/adspr.jsn" ]; then
		sudo install -o 0 -g 0 -m 0644 "$X/fw/modem-all/adspr.jsn" "$local/fw/adsp/"
	fi
	if [ -f "$X/fw/modem-all/adspua.jsn" ]; then
		sudo install -o 0 -g 0 -m 0644 "$X/fw/modem-all/adspua.jsn" "$local/fw/adsp/"
	fi
	sudo install -m 0644 "$TOOLS/../userland/adsp/adspd.boot" "$local/adspd.boot"
	if [ -f "$X/fw/tfa98xx.cnt" ]; then
		sudo install -o 0 -g 0 -m 0644 "$X/fw/tfa98xx.cnt" "$local/fw/tfa98xx.cnt"
		jobs+=(adspd)
		echo "ADSP speaker stack: installed; boot job enabled"
	else
		echo "ADSP speaker stack: binaries/firmware staged; no boot job (extras/fw/tfa98xx.cnt missing)"
	fi
fi
# 2026-11-20: everything the old phone root had by hand goes into the image (a fresh 80g root came up without the
# GPU driver, Bluetooth, the switch files and the debugging tools).
# GPU: the NativeAdrenoMetal bundle (newest build: METAL_BUNDLE, default the iOS 27 plugin build folder) + .publish.
MB=${METAL_BUNDLE:-$TOOLS/../work/UniXNU/build/adreno-metal-plugin27/NativeAdrenoMetal.bundle}
if [ -f "$MB/NativeAdrenoMetal" ] && [ -f "$MB/libvulkan_freedreno.dylib" ]; then
	ext="$R/System/Library/Extensions"
	sudo rm -rf "$ext/NativeAdrenoMetal.bundle"
	sudo cp -R "$MB" "$ext/NativeAdrenoMetal.bundle"
	sudo chown -R 0:0 "$ext/NativeAdrenoMetal.bundle"
	sudo find "$ext/NativeAdrenoMetal.bundle" -type d -exec chmod 0755 {} \;
	sudo find "$ext/NativeAdrenoMetal.bundle" -type f -exec chmod 0644 {} \;
	sudo chmod 0755 "$ext/NativeAdrenoMetal.bundle/NativeAdrenoMetal" "$ext/NativeAdrenoMetal.bundle/libvulkan_freedreno.dylib"
	# 2026-11-27: lower their PAC instructions (blraaz, retab, xpaci, ...) as the system's code was: the Kryo 485 has no
	# FEAT_PAuth, so each one trapped into the kernel's emulation (sleh.c) -- every Metal/Vulkan call and return.
	# NativeAdrenoMetal ~2,900 words, libvulkan_freedreno ~11,500. Settings scrolling 37-39 -> 55-57 fps. Idempotent.
	sudo python3 "$TOOLS/pac/lower-macho.py" "$ext/NativeAdrenoMetal.bundle/NativeAdrenoMetal" \
		"$ext/NativeAdrenoMetal.bundle/libvulkan_freedreno.dylib"
	sudo install -o 0 -g 0 -m 0644 /dev/null "$ext/NativeAdrenoMetal.publish"
	echo "GPU: NativeAdrenoMetal.bundle from $MB ($(shasum -a 256 "$MB/NativeAdrenoMetal" | cut -c1-16))"
else
	echo "ERROR: no NativeAdrenoMetal bundle at $MB (set METAL_BUNDLE): the root would have no GPU" >&2; exit 1
fi
# Switch files the phone had (read at process start): rs-atcall = backboardd encodes at the call (kept 2026-11-20),
# rotate-flip = landscape framebuffer orientation (2026-11-19).
sudo install -d -m 0755 "$R/private/var/local"
for f in sweet-rs-atcall sweet-rotate-flip; do sudo install -o 0 -g 0 -m 0644 /dev/null "$R/private/var/local/$f"; done
# Bluetooth: btbridge (UART <-> bluetoothd), started at boot, retried until wland has the rails up.
if [ -f "$U/btbridge" ]; then
	sudo install -d -m 0755 "$R/private/var/local/bin"
	sudo install -m 0755 "$U/btbridge" "$R/private/var/local/bin/btbridge"
	jobs+=(btbridge)
	echo "btbridge: installed; boot job enabled"
else
	echo "ERROR: no userland/out/btbridge (sh userland/build.sh): the root would have no Bluetooth" >&2; exit 1
fi
# Tools and helper jobs: spinwatch (restarts locationd when it spins a core), bootlog (first 4 min of oslog per boot),
# tsample / ptop / oslog for debugging.
sudo install -d -m 0755 "$R/private/var/local/bin"
for t in spinwatch tsample ptop oslog mgtool perfstate; do
	[ -f "$U/$t" ] && sudo install -m 0755 "$U/$t" "$R/private/var/local/bin/$t" && echo "tool: $t"
done
[ -f "$U/spinwatch" ] && jobs+=(spinwatch)
# MobileGestalt answers (Bluetooth's WifiVendor, cellular, haptics, @2x layout): the cache only exists after the first
# boot, so a boot job applies them whenever the cache is new (userland/jb/sweet-mgfix.sh).
[ -f "$U/mgtool" ] || { echo "ERROR: no userland/out/mgtool: Bluetooth and cellular need it" >&2; exit 1; }
sudo install -m 0755 "$TOOLS/../userland/jb/sweet-mgfix.sh" "$R/private/var/local/bin/sweet-mgfix"
jobs+=(mgfix)
# GPS through the modem (userland/wland/gnss.c): on, with the engine lock lifted for our client.
echo unlock=all | sudo tee "$R/private/var/local/gnss.on" >/dev/null
[ -f "$U/oslog" ] && jobs+=(bootlog)
if [ ${#jobs[@]} -gt 0 ]; then
sudo python3 - "$R/System/Library/xpc/launchd.plist" "${jobs[@]}" <<'PYEOF'
import plistlib, sys
p, want = sys.argv[1], sys.argv[2:]
J = {
    'wland': ('/System/Library/LaunchDaemons/com.sweet.wland.plist', {
        'Label': 'com.sweet.wland', 'ProgramArguments': ['/var/local/bin/wland', 'up', '0'], 'RunAtLoad': True,
        # PMEM_DIRECT=1 (2026-11-26): /dev/pmem through a direct mapping once the radio is up (userland/wland/pmem.c)
        'EnvironmentVariables': {'PMEM_DIRECT': '1'},
        'POSIXSpawnType': 'Interactive', 'StandardOutPath': '/var/local/wland.out',
        'StandardErrorPath': '/var/local/wland.out'}),
    'sshd': ('/System/Library/LaunchDaemons/com.sweet.sshd.plist', {
        'Label': 'com.sweet.sshd', 'ProgramArguments': ['/var/jb/usr/sbin/sshd', '-D', '-e'], 'RunAtLoad': True,
        'KeepAlive': True, 'POSIXSpawnType': 'Interactive', 'StandardErrorPath': '/var/local/sshd.out'}),
}
J['pwdmkdb'] = ('/System/Library/LaunchDaemons/com.sweet.pwdmkdb.plist', {
    # Procursus' libiosexec looks users up in /var/jb/etc/pwd.db / spwd.db only; the bootstrap tar has none
    # (normally its postinst runs pwd_mkdb). Without them sshd sees every user as invalid.
    'Label': 'com.sweet.pwdmkdb', 'ProgramArguments': ['/var/jb/usr/sbin/pwd_mkdb', '-p', '/var/jb/etc/master.passwd'],
    'RunAtLoad': True, 'POSIXSpawnType': 'Interactive', 'StandardOutPath': '/var/local/pwdmkdb.out',
    'StandardErrorPath': '/var/local/pwdmkdb.out'})
J['displayd'] = ('/System/Library/LaunchDaemons/com.sweet.displayd.plist', {
    'Label': 'com.sweet.displayd', 'ProgramArguments': ['/var/local/bin/sweet-displayd'], 'RunAtLoad': True,
    'KeepAlive': True, 'POSIXSpawnType': 'Interactive', 'StandardOutPath': '/var/local/displayd.out',
    'StandardErrorPath': '/var/local/displayd.out'})
J['gnssfeed'] = ('/System/Library/LaunchDaemons/com.sweet.gnssfeed.plist', {
    # GPS through the modem (userland/wland/gnss.c -> /var/local/gnss.fix -> userland/jb/sweet-gnssfeed.m -> locationd)
    'Label': 'com.sweet.gnssfeed', 'ProgramArguments': ['/var/local/bin/sweet-gnssfeed'], 'RunAtLoad': True,
    'KeepAlive': True, 'POSIXSpawnType': 'Interactive', 'StandardOutPath': '/var/local/gnssfeed.out',
    'StandardErrorPath': '/var/local/gnssfeed.out'})
J['bars'] = ('/System/Library/LaunchDaemons/com.sweet.bars.plist', {
    # fake cellular bars / carrier text from /var/local/bars.conf (userland/jb/sweet-bars.m)
    'Label': 'com.sweet.bars', 'ProgramArguments': ['/var/local/bin/sweet-bars'], 'RunAtLoad': True,
    'KeepAlive': True, 'POSIXSpawnType': 'Interactive', 'StandardOutPath': '/var/local/bars.out',
    'StandardErrorPath': '/var/local/bars.out'})
J['adspd'] = ('/System/Library/LaunchDaemons/com.sweet.adspd.plist', {
    'Label': 'com.sweet.adspd', 'ProgramArguments': ['/var/local/bin/adspd', 'serve'], 'RunAtLoad': True,
    'KeepAlive': True, 'EnvironmentVariables': {'ADSPD_BOOT': '30', 'PMEM_DIRECT': '1'}, 'POSIXSpawnType': 'Interactive',
    'StandardOutPath': '/var/local/adspd.out', 'StandardErrorPath': '/var/local/adspd.out'})
J['btbridge'] = ('/System/Library/LaunchDaemons/com.sweet.btbridge.plist', {
    'Label': 'com.sweet.btbridge', 'ProgramArguments': ['/var/local/bin/btbridge', '-q'], 'RunAtLoad': True,
    'KeepAlive': True, 'EnvironmentVariables': {'PMEM_DIRECT': '1'}, 'ThrottleInterval': 10, 'POSIXSpawnType': 'Interactive',
    'StandardOutPath': '/var/local/btbridge.out', 'StandardErrorPath': '/var/local/btbridge.out'})
J['spinwatch'] = ('/System/Library/LaunchDaemons/com.sweet.spinwatch.plist', {
    'Label': 'com.sweet.spinwatch', 'ProgramArguments': ['/var/local/bin/spinwatch', 'locationd'], 'RunAtLoad': True,
    'KeepAlive': True, 'ProcessType': 'Background',
    'StandardOutPath': '/var/local/spinwatch.log', 'StandardErrorPath': '/var/local/spinwatch.log'})
J['mgfix'] = ('/System/Library/LaunchDaemons/com.sweet.mgfix.plist', {
    'Label': 'com.sweet.mgfix', 'ProgramArguments': ['/var/local/bin/sweet-mgfix'], 'RunAtLoad': True,
    'KeepAlive': False, 'POSIXSpawnType': 'Interactive',
    'StandardOutPath': '/var/local/mgfix.out', 'StandardErrorPath': '/var/local/mgfix.out'})
J['bootlog'] = ('/System/Library/LaunchDaemons/com.sweet.bootlog.plist', {
    'Label': 'com.sweet.bootlog', 'RunAtLoad': True, 'KeepAlive': False, 'POSIXSpawnType': 'Interactive',
    'ProgramArguments': ['/bin/zsh', '-c', 'exec > /var/local/boot-oslog.txt 2>&1; '
                         'for p in /usr/local/bin/oslog /var/local/bin/oslog; do '
                         '[ -x $p ] && exec $p -t 240; done; echo no oslog']})
d = plistlib.load(open(p, 'rb'))
for w in want:
    k, j = J[w]
    d['LaunchDaemons'][k] = j
data = plistlib.dumps(d, fmt=plistlib.FMT_BINARY)
with open(p, 'r+b') as f:
    f.write(data)
    f.truncate()
print('launchd.plist: added', ', '.join(J[w][1]['Label'] for w in want))
PYEOF
sudo install -d -m 0755 "$R/private/var/local"
fi

# logd stores (owner _logd 272, .migrated markers) and the system keybag, as for iOS 16.
db="$R/private/var/db"
sudo install -d -o 272 -g 272 -m 0751 "$db/diagnostics"
sudo install -d -o 272 -g 272 -m 0755 "$db/uuidtext" "$db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do
	sudo install -o 272 -g 272 -m 0644 /dev/null "$db/$d/.migrated"
done
sudo install -d -m 0700 -o 0 -g 0 "$R/private/var/keybags"
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' |
	sudo tee "$R/private/var/keybags/systembag.kb" >/dev/null
sudo chown 0:0 "$R/private/var/keybags/systembag.kb"; sudo chmod 0600 "$R/private/var/keybags/systembag.kb"

df -h "$R"
hdiutil detach "$R"
trap - EXIT
# The partition gets the bare volume: a raw image (UDTO) of the sparse image, no partition map.
hdiutil convert ios27-root.sparseimage -format UDTO -o ios27-root
ls -l ios27-root.cdr
shasum -a 256 ios27-root.cdr
# Sparse image for fastboot: ONLY with tools/hfs2simg.py. A generic converter drops all-zero blocks that HFS uses
# (2026-11-20: corrupted dyld and the cache on the phone, launchd crashed).
python3 "$TOOLS/hfs2simg.py" ios27-root.cdr ios27-root.simg
