# mk (minkernel)

`mk` is a lightweight MTK-focused chainloader project.

Design goals:
- Keep core boot flow small and auditable.
- Separate SoC/platform code from device quirks.
- Parse `extlinux.conf` and boot Linux payloads.
- Avoid EDK2-style layout and complexity.

Current status:
- Initial scaffold with clear module boundaries.
- Phase 1 baseline: raw block reads + GPT header/entry scanning.
- SoC driver registry is in place.
  - `mt6765`: functional storage/GPT path
  - `mt6761`: functional storage/GPT path
- MTK backends share a common block I/O layer with per-SoC open hooks.
- Device profiles provide:
  - block-device probe path lists
  - panel compatibility hints
  - default boot/root labels
- MTK backends now consume panel hints during init:
  - detect from `/proc/device-tree/compatible` when available
  - fallback to profile-preferred panel
- Host-buildable development binary (`mk-dev`) for fast iteration.

Quick usage:
- Build: `make -C mk`
- Build ARM64 stage0 entry artifact (freestanding):
  - `make -C mk stage0`
- Build ARM64 Linux-Image-header-compatible payload (no Linux kernel):
  - `make -C mk stage0-kpayload`
  - outputs:
    - `mk/out/stage0/mk-kpayload.bin`
    - `mk/out/stage0/mk-kpayload.bin.gz`
- Build no-kernel boot.img in one command (uses internal target layout DB):
  - `make -C mk bootimg-nokernel DEVICE=oppo-a16`
  - output:
    - `mk/out/bootimg/mk-oppo-a16-boot.img`
- List supported device profiles:
  - `mk/mk-dev --list-devices`
- List GPT partition names:
  - `mk/mk-dev --device oppo-a16 --list /dev/block/by-name/userdata`
  - or `MK_BLOCK_DEV=/path/to/disk.img mk/mk-dev --list`
- Find one partition:
  - `mk/mk-dev --device oppo-a16 recovery /dev/block/by-name/userdata`
- Select SoC driver:
  - `MK_SOC=mt6765 mk/mk-dev --list /dev/block/by-name/userdata`
- Select device via env:
  - `MK_DEVICE=oppo-a16 mk/mk-dev --list`

Direct-flash scaffolding (no initramfs workflow):
- Stage0 entrypoint artifacts:
  - `mk/out/stage0/mk-stage0.elf`
  - `mk/out/stage0/mk-stage0.bin`
- Android boot.img pack helper:
  - `mk/tools/pack-bootimg.sh --device oppo-a16 --kernel <Image.gz> --out mk-boot.img`
  - list internal layouts:
    - `mk/tools/pack-bootimg.sh --list-devices`
  - layout files:
    - `mk/devices/layouts/oppo-a16.env`
    - `mk/devices/layouts/mt6761-ref.env`
  - MTK-style auto-derive from dumped stock boot image:
    - `mk/tools/pack-bootimg.sh --kernel <Image.gz> --out mk-boot.img --from-stock /path/to/boot.img`
  - Optional second blob:
    - `mk/tools/pack-bootimg.sh ... --second mk/out/stage0/mk-stage0.bin`
  - Samsung/legacy compatibility flags:
    - `--append-seandroidenforce`
    - `--normalize-v0` (for header v0 images)

`pack-bootimg.sh` behavior:
- prefers lk2nd's `mkbootimg` script when available
- uses a 1-byte ramdisk by default
- supports internal per-device layout defaults (no manual offsets)
- supports stock-image introspection when you explicitly want re-derivation

No-kernel experiment flow (boot header + mk payload):
```bash
make -C mk bootimg-nokernel DEVICE=oppo-a16
```

Important:
- This repo now includes packaging/entrypoint scaffolding, but the current
  device bootloader still decides what payloads are executable.
- A directly bootable mk path still requires the kernel/boot chain handoff
  integration to transfer control into mk logic.

Planned runtime stages:
1. Storage + GPT + partition lookup. (in progress)
2. Filesystem reader + extlinux parser.
3. Payload loader + Linux handoff.
4. Fastboot + simple UI (optional).
