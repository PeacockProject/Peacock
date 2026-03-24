# mk (minkernel)

`mk` is a baremetal ARM64 chainloader for MediaTek devices.

It replaces the stock bootloader's kernel payload, runs as a freestanding
aarch64 binary injected into the boot image, and chainloads Linux from an
ext2 partition using extlinux.conf.

Design goals:
- Keep core boot flow small and auditable.
- Separate SoC/platform code from device quirks.
- Parse `extlinux.conf` and boot Linux payloads.
- Avoid EDK2-style layout and complexity.

Current status:
- Boots Linux to OpenRC on OPPO A16 (MT6765/MT6357).
- Fastboot USB with download, flash, getvar, and `oem` commands.
- Display driver with DSI panel init, OVL overlay, and boot splash.
- On-screen fastboot menu with volume/power key navigation.
- Offline charging mode (charger-only boot detection, power off on unplug).
- Boot status display (decompressing, loading initramfs, handoff).
- ext2 filesystem reader, gzip/zlib decompressor, FDT patcher.
- Watchdog management, PMIC access (MT6357 via pwrap).

## Building

```bash
make -C mk            # builds stage0 (default target)
make -C mk clean      # clean build artifacts
```

Outputs:
- `mk/out/stage0/mk-stage0.elf` / `.bin` — stage0 entry stub
- `mk/out/stage0/mk-kpayload.elf` / `.bin` / `.bin.gz` — kernel payload

## Boot image

Build a flashable boot.img:
```bash
make -C mk bootimg-nokernel DEVICE=oppo-a16
```
Output: `mk/out/bootimg/mk-oppo-a16-boot.img`

The pack helper supports:
- Internal per-device layouts: `mk/devices/layouts/oppo-a16.env`
- Stock image introspection: `--from-stock /path/to/boot.img`
- List devices: `mk/tools/pack-bootimg.sh --list-devices`

## Source layout

```
stage0/aarch64/          — baremetal aarch64 source (the actual bootloader)
  kernel_payload_main.c  — boot orchestrator
  mk_boot.c             — Linux boot chain (extlinux, decompress, FDT, jump)
  mk_fdt.c              — FDT read/query/patch
  mk_wdt.c              — watchdog timer
  mk_pmic.c             — PMIC wrapper (MT6357)
  mk_ui.c               — display, menus, charging screen, boot status
  mk_ext2.c             — ext2 filesystem reader
  mtk_usb.c             — USB PHY + fastboot protocol
  mtk_display.c          — DSI/OVL display backend
  mtk_panel.c           — panel profiles
  mtk_dsi.c / mtk_gpio.c / mtk_i2c.c / mtk_pwm.c — peripheral drivers
  mk_common.h           — shared inline helpers
devices/                 — per-device profiles and boot image layouts
tools/                   — pack-bootimg.sh
```
