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
- [x] Replace the `prp/vendor/<device>/rootfs-runtime` lookup in
  `internal/mkinitfs/mkinitfs.go` with a peacock-ports package build.
  `InitConfig` now carries `UtilLinuxBuildDir` + `Lvm2BuildDir`;
  `cmd/peacock/build.go` builds the util-linux + lvm2 ports via the new
  `buildPortForInitramfs` helper (built on top of `buildPackageInChrootStep`)
  and plumbs the build dirs through. `runtimeVendorCandidates` now consumes
  `UtilLinuxBuildDir`; the `prp/vendor` and `prp/out` candidate roots are
  gone. The dmsetup lookup + lib search now prefer
  `Lvm2BuildDir/sbin/dmsetup` and `Lvm2BuildDir/{lib,usr/lib,stage/...}`
  with host paths as a final fallback.
- [/] Install the canonical subparts-mount shell library into the initramfs.
  `mkinitfs.Build` now drops `assets/initramfs/subparts-mount.sh` (with
  legacy `prp/initramfs/rootfs/usr/lib/prp/subparts-mount.sh` fallback) at
  `/usr/lib/peacock/subparts-mount.sh`, mode 0755. Remaining work: switch
  the inline init shell to `. /usr/lib/peacock/subparts-mount.sh`, delete
  the inline `setup_prp_like_subparts` function, and rename the
  `PRP-subparts:` log prefix to `subparts:`. The inline function now
  carries a note pointing at the canonical implementation pending that
  follow-up.

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
  Initial coverage: `xiaomi-daisy` (msm8953), `samsung-jflte` (msm8960/msm8660 — verify).
  More device variants will follow the same pattern.

## Assets

- [x] Commit `assets/conspiracy.png` so the initramfs can include it without relying
  on a path that varies by checkout. Lookup order is already
  `conspiracy.png`, `assets/conspiracy.png`, `prp/assets/conspiracy.png` in
  `internal/mkinitfs/mkinitfs.go:1554-1557`; the canonical location going forward
  is `assets/`.
