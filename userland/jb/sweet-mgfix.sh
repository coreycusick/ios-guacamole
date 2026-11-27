#!/bin/zsh
# sweet-mgfix: re-apply the MobileGestalt answers this phone needs, whenever MobileGestalt's cache is new (fresh root,
# or a rebuild after a device-tree change drops hand-set answers). Boot job com.sweet.mgfix (tools/mkios27root-mac.sh).
# 2026-11-20. Changes take effect for processes that read them later; after the very first boot of a fresh root,
# reboot once (as for the IMEI).
#   WifiVendor USI                 BlueTool picks the Bluetooth chip's boot script by it (else no Bluetooth)
#   HasBaseband, cellular-data, telephony, CellularTelephonyCapability   cellular stack, Cellular settings row
#   DeviceSupportsHaptics, DeviceSupportsClosedLoopHaptics, ringer-switch
#   (and every boot: perfstate 1, powerlogd's perfManagementState for Battery Health)
#   ArtworkTraits/ArtworkDeviceSubType 1792, ArtworkDeviceScaleFactor 2   the @2x (828x1792) layout, no rdar bar
# iOS has no sleep/touch/date binaries: zsh modules instead (2026-11-20).
zmodload zsh/zselect zsh/datetime
now() { strftime '%Y-%m-%d %H:%M:%S' $EPOCHSECONDS; }
pause() { zselect -t $(( $1 * 100 )); }
cache=/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist
mark=/var/local/mgfix.done
mg=/var/local/bin/mgtool
# 2026-11-27: Battery Health needs powerlogd's perfManagementState (PerfPowerServices is not in this root):
# 1 = normal peak performance (this port never throttles for battery age). Every boot; cheap.
if [ -x /var/local/bin/perfstate ]; then
	/var/local/bin/perfstate 1 && { /var/jb/usr/bin/killall cfprefsd 2>/dev/null || killall cfprefsd; }   # exit 3: already 1
fi
for i in {1..120}; do [ -f $cache ] && break; pause 1; done
[ -f $cache ] || { echo "$(now) no MobileGestalt cache after 120 s"; exit 0; }
if [ -f $mark ] && [ ! $cache -nt $mark ]; then echo "$(now) cache unchanged, nothing to do"; exit 0; fi
pause 5	# let MobileGestalt finish writing a new cache
$mg setstr WifiVendor USI
for k in HasBaseband cellular-data telephony CellularTelephonyCapability DeviceSupportsHaptics \
	DeviceSupportsClosedLoopHaptics ringer-switch; do $mg setbool $k true; done
$mg setint ArtworkTraits/ArtworkDeviceSubType 1792
$mg setint ArtworkTraits/ArtworkDeviceScaleFactor 2
: > $mark	# creates it / sets its mtime to now
echo "$(now) MobileGestalt answers applied (reboot once if this was the first boot of a fresh root)"
