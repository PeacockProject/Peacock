# Peacock Porting Tasks

- [x] Remove legacy `i9500` device files
- [x] Build `samsung-jflte` device image
- [x] Verify `samsung-jflte` build artifacts
- [/] Build `samsung-jflte` with OpenRC

## Initramfs generation

- [/] Extract `internal/mkinitfs` into a standalone distro package
  (`peacock-initramfs-tools`).
  First-pass extraction landed: the ~750-line `initScriptTemplate` raw-string
  and the ~40-line `initWrapperSource` raw-string have moved out of
  `internal/mkinitfs/mkinitfs.go` into
  `Peacock/assets/initramfs/{init.sh.in,init-wrapper.go.in}` and are
  consumed via `os.ReadFile`. `mkinitfs.go` shrank from 1372 LOC to
  651 LOC; only Go remains in there.
  New port `peacock-ports/base/peacock-initramfs-tools/` ships the three
  initramfs assets (`init.sh.in`, `init-wrapper.go.in`,
  `subparts-mount.sh`) to `/usr/lib/peacock/`. `cmd/peacock/build.go` now
  builds it via `buildPortForInitramfs` and plumbs the result through
  `InitConfig.InitramfsToolsBuildDir`. Lookup order in `mkinitfs` is:
  port build dir → `assets/initramfs/` → legacy `prp/initramfs/rootfs/`.
  Substitution still uses Go's `text/template` (the existing
  `{{.RootLabel}}` / `{{.InitSystem}}` / `{{if .EnableS4CameraLED}}`
  syntax) — converting to `@PEACOCK_*@` sed placeholders was deferred
  because `EnableS4CameraLED` needs a real `if/else` block, not a single
  substitution.
  Remaining work:
    1. Keep `Peacock/assets/initramfs/*` and
       `peacock-ports/base/peacock-initramfs-tools/*` in sync. Today
       they're duplicated; a future cleanup could symlink, tarball, or
       script the port's build to pull from `Peacock/assets/initramfs/`
       at peacock-build time.
    2. The OpenRC inittab heredoc nested inside `init.sh.in` (lines
       around the `cat > /new_root/etc/inittab` block) is still inline
       shell. Extracting it would require either a separate
       `inittab.template` shipped by the port, or just leaving it as
       generated config — low value, deferred.
    3. The `{{if .EnableS4CameraLED}}` block is a debug-only branch; if
       sed placeholders ever replace text/template, this needs to become
       a separate sourced helper or stay in-file with a runtime
       `PEACOCK_S4_LED=1` env switch.
    4. Verify byte-similarity (not byte-equality, since the new
       `init.sh.in` has a documentation header) of the rendered cpio on
       a real device boot — see Constraints note below.
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
