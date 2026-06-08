# Peacock Porting Tasks

- [x] Remove legacy `i9500` device files
- [x] Build `samsung-jflte` device image
- [x] Verify `samsung-jflte` build artifacts
- [/] Build `samsung-jflte` with OpenRC

## Initramfs generation

- [x] Extract `internal/mkinitfs` into a standalone distro package
  (`peacock-mkinitfs`).
  Standalone repo: PeacockProject/peacock-mkinitfs.
  The full mkinitfs pipeline (cpio assembly, init wrapper compile, asset
  templating) now lives there as a cobra-driven binary; the three
  template/library assets (`init.sh.in`, `init-wrapper.go.in`,
  `subparts-mount.sh`) are embedded via `//go:embed`. The Peacock CLI
  no longer carries `internal/mkinitfs/` or `assets/initramfs/`; it
  builds the `base/peacock-mkinitfs` port and execs
  `<portDir>/usr/bin/peacock-mkinitfs build ...` out-of-process,
  falling back to `$PATH` for dev installs. Template substitution is
  still Go's `text/template` (the `{{.RootLabel}}` / `{{.InitSystem}}`
  / `{{if .EnableS4CameraLED}}` syntax).
  Remaining work:
    1. The OpenRC inittab heredoc nested inside `init.sh.in` (lines
       around the `cat > /new_root/etc/inittab` block) is still inline
       shell. Extracting it would require either a separate
       `inittab.template` shipped by the binary or leaving it as
       generated config — low value, deferred.
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
- [x] Install the canonical subparts-mount shell library into the initramfs.
  `mkinitfs.Build` now drops `assets/initramfs/subparts-mount.sh` (with
  legacy `prp/initramfs/rootfs/usr/lib/prp/subparts-mount.sh` fallback) at
  `/usr/lib/peacock/subparts-mount.sh`, mode 0755. The embedded init now
  sources that file and calls the `setup_subparts_root_dev` wrapper in
  place of the deleted inline `setup_prp_like_subparts`; the
  `PRP-subparts:` log prefix is gone.

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
