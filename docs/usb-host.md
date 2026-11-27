# USB accessories (host mode) on guacamole — plan from source

Sources (git-ignored, fetched by `sh outputs/fetch-usb-src.sh` into work/linux-src):
- OnePlus SM8150 kernel (branch oneplus/SM8150_P_9.0, Linux 4.14.83): drivers/power/supply/qcom/{qpnp-smb5.c,
  smb5-lib.c, smb5-reg.h}, drivers/usb/dwc3/{dwc3-msm.c, core.c, host.c}, drivers/usb/phy/{phy-msm-snps-hs.c,
  phy-msm-ssusb-qmp.c}, drivers/usb/host/xhci*.c
- mainline Linux (master): the same areas, cleaner xHCI reference.
Live Android DT extracts: research/usb/live-dt-usb0.txt (ssusb@a600000, dwc3, hsphy@88e2000, ssphy@88e8000),
research/usb/live-dt-smb5-typec.txt (PM8150B charger + Type-C @0x1500, qcom,otg-cl-ua 1.5 A).

## Step 1: Type-C dual role + 5 V VBUS (done in kernel, 2026-11-26, SweetBattery setupTypec/checkTypec)
- Android default: sink only (TYPE_C_MODE_CFG 0x1544 = EN_SNK_ONLY); the Settings OTG switch sets EN_TRY_SNK.
- We set EN_TRY_SNK at boot (boot-arg sweetotg=0: leave sink-only), VBUS under SW control (0x1153 bit1 = 0),
  OTG current limit 1.5 A (0x1152 = 2).
- Every second: attached (0x1509 b5) + source mode (0x150b b6) + sink seen (0x1508 Rd-open b3 / Rd-Ra b2)
  -> DCDC_CMD_OTG 0x1140 b0 = 1; otherwise 0. VBUS error (0x150b b4) -> off.
- Our own VBUS makes USBIN "present": the charger logic ignores USBIN while OTG is on.
- Not done: VCONN (0x1546), water/LPD detection, PD.

## Step 2: DWC3 host + xHCI (next)
- dwc3-msm.c dwc3_otg_start_host: PHY_HOST_MODE on the HS PHY, VBUS on, GCTL PRTCAPDIR = host (1),
  dwc3_host_init -> generic xhci-plat on the same register block (xHCI capability regs at 0x0a600000 + 0).
- maximum-speed in DT is super-speed but the QMP SS PHY is a DP combo PHY; start USB 2.0 only
  (GUSB3PIPECTL suspend, port 2 / SS ignored), as fastboot leaves the HS PHY configured.
- XNU has no xHCI driver in source (Apple's AppleUSBXHCI is not in the open kernel), so a minimal one:
  command ring, event ring (polled thread like sweet_usb.c), one device slot, control transfers for
  enumeration, interrupt IN endpoints. DMA not coherent: clean/invalidate as sweet_usb.c does.

## Step 3: HID
- Boot protocol keyboard / mouse first (SET_PROTOCOL 0), report descriptors later for game controllers.
- Publish as IOHIDEventService (as SweetHID.cpp does for touch) so backboardd gets keyboard and pointer events.

## Step 4: mass storage (later)
