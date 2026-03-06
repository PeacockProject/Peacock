# mk Roadmap

## Phase 0: Skeleton (now)
- Repo layout for core/hal/devices/apps.
- Host-build stub (`mk-dev`).
- SoC registry + pluggable driver model. (done)
- Device profile registry with per-device block probe tables and panel hints. (done)
- SoC init consumes panel hints via DT compatible matching + fallback. (done)
- ARM64 freestanding stage0 entry artifact target. (done, scaffolding)
- Android boot.img packaging helper for direct-flash experiments. (done, scaffolding)
- Internal per-device boot image layout database + one-shot no-kernel bootimg target. (done, scaffolding)

## Phase 1: Boot Storage
- Real block reads for MTK storage path. (done)
- Shared MTK block layer + multi-SoC backend wiring. (done)
- GPT parser (header + entry scanning, label lookup). (done)
- GUID checks and CRC validation. (next)

## Phase 2: Boot Selection
- ext4/FAT file loading.
- `extlinux.conf` parser.
- fallback entry logic.

## Phase 3: Handoff
- Load kernel/initramfs/dtb to RAM.
- cmdline assembly.
- Linux arm64 handoff implementation.

## Phase 4: Device UX
- framebuffer text/menu path.
- key input + optional touch input.
- fastboot mode.
