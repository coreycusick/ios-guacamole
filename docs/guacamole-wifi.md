# Guacamole Wi-Fi: Pixel 4 stack experimental initialization

This branch layers `NWylynko/ios-flame`'s WCN3990 Wi-Fi userspace and
`/dev/wlan` + `en0` + Apple80211 kernel implementation **on top of** the
`OtVaL228/ios-sweet` `guacamole` XNU port. Do not replace the OnePlus kernel,
ADT, shim, boot header, display, UFS or GPU paths with Pixel 4 versions.

## Status (experimental bring-up enabled; not yet compiled or device-tested)

- `userland/wifi/`: copied from `ios-flame/phone/wifi`, using the WCN3990
  radio, Qualcomm modem control, WPA2, the `/dev/wlan` bridge, and networking.
  `build.sh` targets the installed iPhoneOS SDK on macOS (`xcrun`, `codesign`).
- `userland/wifi/guac_board.h`: hardware access **enabled on this test
  branch**, at the user's request. The modem, WLAN, MSA and SMMU addresses
  use Corey's merged GM1917 DTB. The 16 MiB DMA pool at `0xe0000000` is
  borrowed experimentally from the Pixel port: the address is inside the
  OnePlus's reported low DDR, outside XNU's highmem allocation window, but
  *firmware ownership at that address is not independently verified*.
  The four PMIC rail votes are still Pixel defaults pending phandle mapping.
  Only the OnePlus's own WLAN BDF and modem firmware may be used.
- `tools/mkadt.py --guacamole --highmem --guacamole-wifi` adds the `wlan`
  node that allows iOS launchd to start wifid. `guac-mkboot.sh` enables it
  by default on this branch (`WIFI=0` to disable), with `sweetpmem=1`,
  `sweetwifid=1`, and `sweetwifi=1` boot arguments. Default `cpus=8`.
- `userland/wifi/kernel-patch-candidates/`: extracted from Pixel patches
  0094 (physical memory), 0102 (RPMh), 0123 (en0/Apple80211/IOKit), and
  0124 (joined BSS cache). **Copied into `xnu/patches/0130`–`0133` on the
  experimental branch** so the XNU build scripts pick them up after the
  upstream GPU patch 0125. They still
  need patch-application checks and a full build against OnePlus XNU 0124.
  Their internal default for `sweetpmem` and `sweetwifid` is 0; the Wi-Fi
  test boot explicitly enables both.
  The Pixel patch's unrelated SweetSurface and key-store changes are excluded.
- `tools/sweet-root-tweaks.py`: launchd includes `org.guacamole.wland`
  (`/usr/libexec/wland auto`, RunAtLoad) in the *experimental* root plist.
  `userland/build.sh` builds `out/wland`; the signed binary still must be
  installed on the phone. Firmware/calibration files are intentionally absent.

No kernel build or phone hardware test has been completed for this integration.
The known-good boot image and root must remain unchanged. **This can crash or
hang the phone** (modem PAS, RPMh votes, SMMU routing). Test with `fastboot
boot`, never a boot-partition flash. Keep recovery and unique radio partitions
intact.

## GitHub Actions builds and GPU black-screen isolation

This branch's `.github/workflows/xnu.yml` runs on a push to
`wifi-pixel4-latest` (or `wifi-pixel4-stage`) whenever `xnu/patches/**`, `userland/wifi/**`, or the
workflow changes. It builds on GitHub-hosted macOS runners, first checks
the staged Wi-Fi input files, and uploads `xnu-kernel`, `xnu-buildtree`, and
separately compiled/ad-hoc-signed `guacamole-wland` artifacts. The kernel
patches **have not been compile-tested yet** and may fail to apply or build;
download the Actions logs to locate the first failing 0130–0133 patch.
The full iOS root/SWEETBOX and the final phone boot image are **not**
created by these jobs; they still require separate careful packaging.

GitHub's `workflow_dispatch` requires the workflow to exist on the fork's
**default branch**, so a first push to this non-default branch should use
the `push` event. After the default branch includes the workflow, it may
also be run via `gh workflow run xnu.yml --ref wifi-pixel4-latest`.

The published OtVaL `guacamole` kernel at 0125 already includes experimental
GPU support, but G4 CoreAnimation surface presentation is not confirmed
working. G2b user client and G3 freedreno offscreen rendering are separate
from visible UI acceleration. Avoid conflating that black screen with Wi-Fi:

| Boot test | Environment | Kernel/UI boot arguments |
| --- | --- | --- |
| Known-good display + experimental Wi-Fi | default | `sweetgpu=0 sweetgl=0` |
| GPU bring-up while keeping software UI | `GPU_STAGE=11 GL_UI=0` | `sweetgpu=11 sweetgl=0` |
| Experimental accelerated iOS UI | `GPU_STAGE=11 GL_UI=1` | `sweetgpu=11 sweetgl=1` |

`tools/guac-mkboot.sh` uses the safe display configuration by default, with
8 CPUs enabled. `GL_UI=1` is for separately controlled G4 testing and may
produce a black screen. OtVaL's 0124 surface patch converts 10-bit wide
colour formats to BGRA unless `sweetsurf10=1`; that fixes a known format
barrier but does not by itself prove G4's complete presentation path.

## Confirmed OnePlus 7 Pro hardware facts

Corey's stock `boot_a.img` DTB merged with DTBO index 28 has:

| Region | Physical base | Size | Notes |
| --- | --- | --- | --- |
| `modem_region` | `0x8dc00000` | `0x09600000` | Pixel starts at same address, but incorrectly assumes `0x0b400000` size |
| `pil_wlan_fw_region` | `0x8bc00000` | `0x00180000` | `icnss` asks for `0x100000` MSA inside this reservation |
| `icnss` controller | `0x18800000` | `0x00800000` | Same WCN register base as Pixel |
| `apps_smmu` | `0x15000000` | `0x00100000` | `icnss` IOMMU stream `0x640`, mask `0x01` |

The OnePlus `icnss` node also sets `qcom,smmu-s1-bypass`, and has four
supplies `vdd-cx-mx` / `vdd-1.8-xo` / `vdd-1.3-rfa` / `vdd-3.3-ch0`.
Their phandles `0xe9`–`0xec` still must be resolved to specific RPMh
resources before transplanting the Pixel 4 power-vote code.

The [published SM8150 DTS](https://android.googlesource.com/kernel/common/+/442489c219235991de86d0277b5d859ede6d8792/arch/arm64/boot/dts/qcom/sm8150.dtsi)
has a different modem base (`0x8d800000`). The handset's *actual* merged
DTB wins here. Do not use mainline's base for firmware loading on the GM1917.

Use the already extracted/decompiled DTS on the Mac (read-only):

```sh
cd ~/ios-guacamole
grep -Eni -C 8 'pil_modem|modem_region|mpss|pil_wlan|wlan_fw|wcn3990|wifi|wlan-msa|apps-smmu|rfclka' \
    work/guacamole-stock/extracted/guacamole-merged.dts
```

If the name or path differs, use the saved merged DTS you already created;
do not use a Pixel DTB or flash anything. Also verify modem and Wi-Fi firmware
paths and calibration/board data from **this OnePlus's own** firmware package.
The static donor still has Pixel-specific power rail votes, a synthetic MAC,
and modem bring-up assumptions. They are now **enabled for experimentation**
at Corey's request, despite the remaining uncertainty. `wland up`, `boot`,
`wlan-power`, `smmu` and `vote` can touch hardware; `wland profile` is
read-only. `wland up` fails early if the WLAN SMMU route cannot be established.

## Stage kernel changes after the OnePlus XNU checkout is ready

Prepare patches from an unmodified Pixel reference checkout:

```sh
python3 tools/guac-wifi-mkpatches.py ../ios-flame-reference
```

The four kernels patches were copied to `xnu/patches/0130`–`0133` on this
branch. The git trees diverged at 0082, so `git apply --check` and a full
kernel build are needed against OnePlus's patched
XNU sources. Resolve conflicts in existing `sweet_rpmh.c`, `sweet_ufs.c`, and
the build manifests, then record new ordered guacamole patches. The donor's `0127`
retains the donor's `flame_*` identifiers internally; they are protocol
names, not a request to use Pixel hardware addresses.

Bring-up order (all initialization paths are now present/enabled in the
experimental source; runtime success is unverified):

1. Resolve the remaining regulator phandles and validate the low-memory DMA
   reservation before running an actual device test, even though source is on.
2. Apply/compile the now-active kernel patch sequence with the OnePlus XNU
   toolchain and build `userland/wifi/out/wland` on the Mac.
3. Install `/usr/libexec/wland`, the modified launchd cache and OnePlus
   firmware in the **experimental** iOS root/SWEETBOX. Preserve known good.
4. Boot with Wi-Fi enabled, collect log checkpoints, then test scan/join/DHCP.

The Pixel4 stack's first verified path is userland radio/WLFW + /dev/wlan,
then `en0`/wifid. GPU/Metal integration is independent and should stay on
its own branch until the basic Wi-Fi control plane is stable.

Attribution: radio and bridge source from
[ios-flame](https://github.com/NWylynko/ios-flame); this itself credits
[ios-topaz](https://github.com/WeWaffles/ios-topaz) and the Redwood network
interface work. Refer to upstream CREDITS and LICENSE before redistribution.
