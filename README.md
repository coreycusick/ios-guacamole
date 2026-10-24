# ios-guacamole

**Experimental port of Apple's iOS 16 to the OnePlus 7 Pro (GM1917).**

A work-in-progress port of [ios-flame](https://github.com/NWylynko/ios-flame), itself based on [ios-sweet](https://github.com/dipa-studio/ios-sweet), targeting the OnePlus 7 Pro (`guacamole`, Qualcomm SM8150 / Snapdragon 855, Adreno 640).

The goal is to boot Apple's XNU kernel (xnu-8796, iOS 16 generation) and run the **unmodified iOS 16.7.16 (20H392) iPhone X userland** directly on the OnePlus 7 Pro.

The OnePlus 7 Pro shares the Snapdragon 855 and Adreno 640 GPU with the Google Pixel 4, making `ios-flame` a promising starting point for the port.

> **Development status: Early bring-up — October 24, 2026**
>
> The repository is currently based on the Pixel 4 implementation. OnePlus-specific hardware support and bootloader integration are planned.
>
> **iOS has not yet been booted on the OnePlus 7 Pro using this fork.**
>
> This is experimental software. Do not flash Pixel 4 boot images or run the original installation scripts on the OnePlus 7 Pro. They may erase userdata or damage the device's software installation.

## Target hardware

| Component | OnePlus 7 Pro |
| --- | --- |
| Device | OnePlus 7 Pro |
| Model | GM1917 |
| Codename | `guacamole` |
| SoC | Qualcomm Snapdragon 855 (SM8150) |
| CPU | 8-core Kryo 485 (4 performance + 4 efficiency cores) |
| GPU | Qualcomm Adreno 640 |
| Storage | 256 GB UFS 3.0 |
| Display | 6.67-inch Fluid AMOLED, 1440 × 3120 |
| Bootloader | Qualcomm/OnePlus ABL |
| Bootloader status | Unlocked on development device |
| Target OS | iOS 16.7.16 (20H392) |
| Target kernel | XNU 8796 |

## Current status

The project is in the initial preparation and hardware adaptation stage.

The original `ios-flame` project has demonstrated iOS 16 running on the Google Pixel 4, including GPU-accelerated rendering, Wi-Fi, storage, touchscreen input, and iOS applications.

These features provide a foundation for the OnePlus port, but **they have not yet been validated on OnePlus hardware**.

### Porting progress

| Component | Status | Notes |
| --- | --- | --- |
| Repository | Forked | Based on `ios-flame` |
| Target hardware | Identified | OnePlus 7 Pro GM1917, Snapdragon 855 |
| Development environment | Planned | WSL2/Linux and Intel macOS Hackintosh |
| Bootloader | Not ported | OnePlus ABL integration required |
| Boot shim | Not ported | Pixel 4 implementation available |
| XNU kernel | Existing upstream implementation | SM8150 support requires validation |
| CPU / SMP | Not tested | Reuse upstream Snapdragon 855 implementation where possible |
| UFS storage | Not tested | Board-specific partition layout must be verified |
| Display | Not implemented | OnePlus AMOLED panel support required |
| GPU / Metal | Not tested | Upstream Adreno 640 implementation available |
| Touchscreen | Not implemented | OnePlus-specific controller support required |
| USB | Not tested | Debugging and networking require adaptation |
| Wi-Fi | Not tested | Firmware and board-specific configuration required |
| iOS userland | Not booted | Target is the iPhone X iOS 16.7.16 userland |
| SpringBoard | Not booted | Dependent on kernel, storage and display bring-up |
| Audio / cellular / camera | Not implemented | Outside initial bring-up scope |

**No working iOS boot on the OnePlus 7 Pro is claimed at this stage.**

## Development strategy

The initial goal is to reuse as much of `ios-flame` as possible while introducing OnePlus-specific hardware support.

### Phase 1 — Bootloader and shim

- Inspect the existing Pixel 4 boot shim.
- Research the OnePlus 7 Pro ABL boot requirements.
- Extract the OnePlus stock boot image and device tree.
- Identify physical memory regions and reserved memory.
- Adapt the shim and boot arguments for `guacamole`.
- Produce a test boot image compatible with OnePlus ABL.
- Attempt temporary booting using `fastboot boot`.

**Milestone:** Execute the custom boot shim on the OnePlus 7 Pro and obtain diagnostic output.

### Phase 2 — XNU kernel bring-up

- Attempt to reuse the existing Pixel 4 XNU kernel build.
- Adapt the Apple Device Tree for the OnePlus 7 Pro.
- Verify CPU initialization, timers and interrupts.
- Adapt board-specific memory mappings.
- Implement or modify drivers where required.
- Rebuild XNU if the existing kernel binary cannot initialize correctly.

**Milestone:** Reach XNU kernel initialization without a fatal exception.

### Phase 3 — Storage and iOS userland

- Adapt the existing Qualcomm UFS implementation.
- Verify the OnePlus storage and partition layout.
- Prepare an iOS HFS+ root filesystem.
- Establish a safe installation and recovery procedure.
- Mount the iOS root filesystem.
- Start `launchd` and essential iOS services.

**Milestone:** Boot into the iOS 16 userland.

### Phase 4 — Display and input

- Initialize the OnePlus 7 Pro AMOLED display.
- Adapt the framebuffer and display pipeline.
- Integrate the existing Adreno 640 GPU implementation.
- Adapt Metal rendering through Turnip and metal2vulkan.
- Implement touchscreen and hardware button support.

**Milestone:** Display and interact with SpringBoard.

### Phase 5 — Additional hardware

After achieving a functional graphical environment, investigate:

- Wi-Fi
- USB networking and SSH
- Battery and charging information
- Audio
- Bluetooth
- Sensors
- Video decoding
- Cameras
- Cellular connectivity

These features are not guaranteed to work.

## Proposed boot process

```text
OnePlus 7 Pro
    |
    v
OnePlus ABL (unlocked bootloader)
    |
    v
fastboot boot guacamole-test.img
    |
    v
Custom guacamole boot shim
    |
    +-- Initialize boot arguments
    +-- Configure memory layout
    +-- Construct Apple Device Tree
    +-- Load modified XNU kernel
    |
    v
XNU 8796 (iOS 16)
    |
    +-- Qualcomm SM8150 hardware support
    +-- UFS storage
    +-- Display and GPU
    +-- Touchscreen and input
    |
    v
iOS 16.7.16 userland
    |
    v
SpringBoard
```

This represents the intended architecture, **not a currently working boot sequence**.

## Development environment

Two environments are planned for development.

### Linux / WSL2

Used for:

- Boot shim development
- ARM64 cross-compilation
- Device-tree generation
- Android boot image packaging
- Qualcomm firmware analysis
- Fastboot tooling

### Intel macOS Hackintosh

Used for:

- XNU kernel compilation
- Apple SDK-dependent builds
- iOS userland utilities
- Toolchain testing

The original `ios-flame` project includes build scripts and workflows that may require specific versions of Xcode, LLVM and other dependencies.

## Repository structure

The project currently inherits its structure from `ios-flame`.

| Path | Purpose |
| --- | --- |
| `xnu/patches/` | XNU patches inherited from ios-sweet and ios-flame |
| `shim/` | Bare-metal boot shim |
| `tools/` | Boot image tools, device-tree generation and debugging |
| `phone/` | iOS-side drivers, services and utilities |
| `NOTES.md` | Original Pixel 4 engineering notes |
| `HANDOFF.md` | Original Pixel 4 development history |
| `RECOVERY.md` | Original Pixel 4 recovery instructions |
| `DEVICE.md` | Original Pixel 4 hardware information |

Existing Pixel 4 documentation and scripts should not be treated as OnePlus 7 Pro installation instructions.

OnePlus-specific build targets, scripts and documentation will be added as development progresses.

## Building

The first objective is to establish a reproducible build process for the OnePlus 7 Pro.

Clone the repository:

```bash
git clone https://github.com/coreycusick/ios-guacamole.git
cd ios-guacamole
```

The upstream project provides the starting point for the shim and XNU build processes.

A complete, validated OnePlus 7 Pro build procedure is **not yet available**.

Until the hardware-specific changes are implemented, existing Pixel 4 build scripts should be treated as references only.

## Safety and recovery

This project involves experimental low-level boot code.

Important precautions:

- Keep a copy of the correct GM1917 stock firmware.
- Back up device-specific partitions, including modem and calibration data, before destructive experiments.
- Do not flash Pixel 4 images onto the OnePlus.
- Do not overwrite userdata until the OnePlus storage layout and recovery process have been verified.
- Prefer temporary `fastboot boot` tests where supported.
- Never share personal device calibration or modem partition backups publicly.

The upstream iOS implementation disables or replaces parts of Apple's security model. Even if successfully ported, it should not be treated as a secure everyday operating system.

## Upstream functionality

The original [ios-flame](https://github.com/NWylynko/ios-flame) project has demonstrated the following on the Pixel 4:

- Eight-core Snapdragon 855 operation
- UFS storage and read/write iOS filesystem
- Adreno 640 GPU rendering
- Metal translation using Turnip and metal2vulkan
- SpringBoard and stock iOS applications
- Touchscreen and hardware buttons
- Wi-Fi
- USB networking and SSH

These are **upstream Pixel 4 achievements**, not currently supported features of `ios-guacamole`.

## Credits

This project builds upon the work of:

- [ios-sweet](https://github.com/dipa-studio/ios-sweet) — Original iOS 16 port to non-Apple ARM64 hardware.
- [ios-flame](https://github.com/NWylynko/ios-flame) — Snapdragon 855 / Pixel 4 implementation.
- [ios-topaz](https://github.com/MakrSas/ios-topaz) — Additional Qualcomm hardware and Wi-Fi work.
- [UniXNU](https://github.com/Un1c0rn0x00/UniXNU) — Related XNU and GPU research.
- Mesa / Turnip — Open-source Qualcomm Adreno Vulkan drivers.
- metal2vulkan — Metal translation work.

See the inherited `CREDITS.md` for additional contributors and license information.

---

**Project status:** Early bring-up / hardware porting preparation.

**Target:** OnePlus 7 Pro GM1917 — iOS 16.7.16.

**Last updated:** October 24, 2026.
