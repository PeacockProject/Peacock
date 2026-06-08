# Peacock Porting Tasks

- [x] Remove legacy `i9500` device files
- [x] Build `samsung-jflte` device image
- [x] Verify `samsung-jflte` build artifacts
- [/] Build `samsung-jflte` with OpenRC

## Initramfs generation

- [ ] Extract `internal/mkinitfs` into a standalone distro package (`peacock-initramfs`?).
  Today it lives in-tree as 1.6k lines of Go assembling the cpio. Packaging it would
  let the Peacock host tool stay focused on orchestration and let the initramfs be
  versioned/installed via pacman like everything else in peacock-ports.
- [ ] Replace the `prp/vendor/<device>/rootfs-runtime` lookup in
  `internal/mkinitfs/mkinitfs.go:1243-1264` with a peacock-ports package build.
  Stub manifests landed at `peacock-ports/base/util-linux` and
  `peacock-ports/base/lvm2`. Wire `runtimeVendorCandidates` and the dmsetup
  lookup to consume their built artifacts (sbin/, lib/) instead of the now-empty
  `prp/vendor/` path. Drop the `prp/vendor` + `prp/out` candidate roots once the
  package path is verified.
- [ ] Install the canonical subparts-mount shell library into the initramfs.
  `assets/initramfs/subparts-mount.sh` is a copy of PRP's
  `initramfs/rootfs/usr/lib/prp/subparts-mount.sh`. Have `mkinitfs` drop it at
  `/usr/lib/peacock/subparts-mount.sh` in the cpio and source it from the init
  shell so the inline subparts logic in `mkinitfs.go:600-840` can be replaced
  by a single `. /usr/lib/peacock/subparts-mount.sh`. Rename the log prefix from
  `PRP-subparts:` to `subparts:` during the move.

## Bootloaders as ports

- [/] Add `peacock-ports/device/minkernel-<device>` packages that pull from
  `PeacockProject/MinKernel`, build `make -C mk DEVICE=<dev> bootimg-nokernel`,
  and stage the resulting `mk-<device>-boot.img` for the Peacock image stage to
  flash. Avoids the embedded-mk-binaries problem in this repo entirely.
  Initial coverage: `oppo-a16`. More device variants will follow the same pattern.
- [/] Add `peacock-ports/device/lk2nd-<device>` packages that pull from
  `PeacockProject/lk2nd_peacock` and build the right target (e.g.
  `make TOOLCHAIN_PREFIX=... msm8953 LK2ND_DEVICE=<dev>`) for qcom devices.
  Once these exist, mk and lk2nd are versioned/installed like any other
  device-firmware port instead of being out-of-tree clones.
  Initial coverage: `xiaomi-daisy` (msm8953). More device variants will follow the same pattern.

## Assets

- [ ] Commit `assets/conspiracy.png` so the initramfs can include it without relying
  on a path that varies by checkout. Lookup order is already
  `conspiracy.png`, `assets/conspiracy.png`, `prp/assets/conspiracy.png` in
  `internal/mkinitfs/mkinitfs.go:1554-1557`; the canonical location going forward
  is `assets/`.
