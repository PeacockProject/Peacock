# Peacock backlog

Areas worth picking up. Cross-repo. Kept separate from `task.md` (per-port porting
checklist) and `~/.claude/plans/linked-honking-treehouse.md` (meta-distro plan).

## In progress / committed this round

These are the items we agreed to tackle and are landing as commits alongside this
file; leaving them here so it's clear what's underway vs. what's still untouched.

- [x] `peacock doctor` subcommand: human + `--json` output, filters by
      `--flavor`, `--device`, `--use-host-chroot`. Probe table lives in
      `internal/host/probe.go`; doctor renders results. Exits non-zero on
      any missing/broken so CI can gate. Follow-ups:
  - [ ] Expand device-family probe coverage beyond `oppo-a16`/`samsung-jflte`/
        `xiaomi-daisy` as new ports land.
  - [ ] Per-flavor `pacman-key`/`apt-key`/`apk-tools` keyring checks (today
        we only check tool presence, not whether the keyrings can verify).
- [/] pmbootstrap-style chroot-per-target build strategy (`peacock build
      --use-host-chroot <flavor>` / `PEACOCK_HOST_CHROOT=<flavor>`). v0
      scaffolding committed:
  - CLI flag wired through `cmd/peacock/host_chroot.go` (no conflicts with
    the in-flight `cmd/peacock/build.go` split — flag registration lives
    in its own file).
  - `internal/host/chroot.go` declares the on-disk layout
    (`~/.local/var/peacock/host-chroots/<flavor>/`), supported-flavor
    list, and bootstrap-tarball URL constants.
  - `internal/host.EnsureHostChroot` is idempotent for an already-present
    rootfs and returns a "not yet implemented" error otherwise.
  - `peacock doctor --use-host-chroot <flavor>` collapses the probe set
    to chroot/tar/curl + the host-chroot rootdir.

  Follow-ups (deferred — not started in this round):
  - [ ] `EnsureHostChroot` actual download path: arch needs the `latest`
        index parsed to pick the dated tarball; debian + alpine URLs are
        deterministic.
  - [ ] First-time toolchain install inside the freshly extracted chroot
        (pacstrap/debootstrap-second-stage/apk add build-base).
  - [ ] Routing existing `runner.RunCmd` calls through the chroot. Cleanest
        shape is a `runner.SetExecPrefix(prefix []string)` knob, but the
        sibling agent splitting `cmd/peacock/build.go` may collide there;
        defer until that split lands.
  - [ ] Bind-mount management for `/dev`, `/proc`, `/sys`, the local
        peacock-ports tree, and the workdir cache.
  - [ ] Per-flavor tarball checksum verification (signed where available).
- [/] Split `cmd/peacock/build.go` (~1.9k LOC) and `internal/builder/chroot_build.go`
      (~828 LOC) by build phase.
- [/] `peacock build --bisect <port>` + table tests for `manifest.ResolvedLayout`,
      `ResolvedPrefix`, `SupportsFlavor`, `internal/config` accessors,
      `internal/builder/flavor_aliases` resolver.
- [/] Top-level README in Peacock, peacock-ports, feather, peacock-mkinitfs;
      device-port walkthrough; SCHEMA.md examples for layout=app / layout=compat.
- [x] PRP-as-port: `peacock-ports/device/prp-<device>/package.toml` for
      oppo-a16, xiaomi-daisy, samsung-jflte. Builds `make bootimg
      TARGET=<device>` and stages
      `/usr/share/peacock/recovery/prp-<device>-recovery.img`. Same
      pattern as minkernel-* / lk2nd-*. Versioned 0.0.30 (PRP commit
      count); switch to tag pinning when PRP starts cutting tags.

## Untouched

### PeacockBuilder Wails app (phase 5+ follow-ups)

Phases 0–4 + bindings landed (`8409ffc`..`0df0cfe`). 9.8 MB Wails binary builds
on Linux; frontend is the adapted React mock at
`cmd/peacock-builder/frontend/`. Open items:

- [ ] **In-process pipeline call.** The locked plan-decision was "in-process";
      Phase 3's `StartBuild` ships as a **subprocess exec** of `peacock build`
      instead, because `RunBuildPipeline` lives in `cmd/peacock` `package main`
      and Go forbids importing main packages. To honour the original intent,
      lift `RunBuildPipeline` + the 5 phase functions + their local types
      (`buildSetup`, `packageOrchestrationResult`, `buildCleanup`, etc.) into a
      non-main package like `internal/pipeline/` or `pkg/buildpipeline/`.
      The DTO + Wails event shape (`build:log`, `build:error`, `build:done`)
      stays stable so the swap is body-only inside `build_runner.go`.
- [ ] **Phase 5 sudo handling.** `cmd/peacock-builder/sudo.go` — check
      `sudo -n true` at startup; on Linux fall back to `pkexec sudo -v`,
      macOS to `osascript` admin prompt, Windows to a UAC manifest. Today
      the GUI launches and any sudo-needing step in the subprocess'd build
      will hang waiting for terminal input.
- [ ] **Phase 6 distribution targets.** `cmd/peacock-builder/Makefile`
      wrapping `wails build -platform linux/amd64` → AppImage via
      `appimagetool`, `darwin/universal` → `.dmg` via `create-dmg`,
      `windows/amd64` → standalone `.exe`. Verify each on a clean platform
      VM. CI matrix (see "CI / automation" below).
- [x] **Phase 7+ peacock-installer.** Calamares-style installer for the
      live ISO. `internal/installer/` landed at 2032 LOC across 11 files
      (`installer.go`, `disks.go`, `partition.go`, `format.go`, `copy.go`,
      `bootloader.go`, `user.go`, `system.go`, `pipeline.go`, `runner.go`,
      `installer_test.go`). 12 test funcs / 34 subtests pass. Wails app at
      `cmd/peacock-installer/` shares the React mock via symlinks back to
      `cmd/peacock-builder/frontend/src/`. Open follow-ups:
  - [ ] **End-to-end exercise on a live USB VM.** `CreateLayout`,
        `FormatPartitions`, `CopyLiveRootfs`, `InstallBootloader`,
        `CreateUser`, `RunInstall` all execute real shell-outs against
        block devices and need root + a live ISO context; unit tests
        can't cover those.
  - [ ] **Rsync exclude list completeness.** Today covers Debian-flavoured
        live-boot; arch airootfs may also want `/var/lib/pacman/sync` +
        `/var/cache/pacman/pkg`. Wire to the peacockos live ISO recipe.
  - [ ] **grub vs grub2 binary name.** Currently calls `grub-install` /
        `grub-mkconfig`. Detect inside the target chroot to also support
        Fedora/RHEL's `grub2-*` names.
  - [ ] **Locale handling parity.** Arch path implemented (`locale-gen`);
        Debian's `update-locale` path is untested. Live ISO ships locales
        pre-generated so this is best-effort.
  - [ ] **Distro group-set map.** `useradd` tries `wheel,audio,video,input,sudo`
        then falls back. Principled per-distro map would be cleaner.
  - [ ] **`PartMode="manual"`** — only `erase` is supported in v0.
  - [ ] **ARM bootloader defaults.** Layout autodetect is x86-only
        (efivars or BIOS). ARM ports need port-specific layouts.
  - [ ] **Live ISO integration.** `peacock-installer` binary needs to be
        bundled into the peacockos live ISO + launched automatically on
        the live session's desktop. Out of scope for now.
- [ ] **Frontend simulated scripts.** `Run.jsx` still ships the mock
      `buildScript` / `installScript` simulations. Phase 4 wired the real
      `window.runtime.EventsOn("build:log", …)` subscriptions but the
      simulated lines are still in the file as fallback for dev mode.
      Strip them once the in-process path is live.

### CI / automation

- [ ] GitHub Actions on every repo. Minimum job set per repo:
  - **Peacock**: `go build ./cmd/... ./internal/...`, `go vet`, `go test ./...`.
  - **peacock-ports**: `python3 tools/phase1-verify.py`, TOML round-trip lint.
  - **feather**: `make build && make test`, `clang-tidy src/*.c`, `file ftr | grep statically`.
  - **peacock-mkinitfs**: `go build && go test`, embed-asset diff check.
  - **MinKernel**: `make -C mk DEVICE=oppo-a16 bootimg-nokernel` smoke build.
  - **lk2nd_peacock**: `make TOOLCHAIN_PREFIX=arm-none-eabi- lk2nd-msm8953` smoke build.
  - **PRP**: shellcheck across `scripts/`, `initramfs/rootfs/`, `overlay/`.
- [ ] Cross-repo coordination: when peacock-ports submodule moves, Peacock's CI
      should refetch and revalidate.
- [ ] `dependabot` or equivalent for Go module updates.
- [ ] No automated linting today: `gofmt -l`, `clang-format -i`, `shellcheck`,
      `taplo fmt` should run pre-commit + in CI.

### Reproducible builds

- [ ] `SOURCE_DATE_EPOCH` discipline across cpio/boot.img/feather archive build steps.
- [ ] Deterministic ordering inside the cpio (currently `find` order).
- [ ] Strip timestamps from generated TOML manifests so identical inputs ⇒ identical
      archive bytes.

### Phase 5 (apps + data lifecycle — held until app-runtime planning)

- [ ] `/apps/<name>/` overlay launcher: mount-namespace + `LD_LIBRARY_PATH` setup,
      `exec` chosen entrypoint.
- [ ] `/data/<app>/` per-app state dirs created at install, prompted on remove,
      backupable as a single unit.
- [ ] App permission model — currently overlay has no sandbox. Mobile-first OS
      needs camera/location/contacts gating. Decide capability model
      (Wayland-style portals vs. Android-style runtime permissions vs. hybrid).
- [ ] App store UI (in PeacockOS) — separate Peacock-shell project; needs its own
      planning round.
- [ ] App update / atomic switch model. Phase 4b feather overwrites; phase 5 should
      version-pin + symlink swap.

### Phase 6 (build farm — server-side, biggest infra effort)

- [ ] `PeacockProject/build-farm` repo: CI runners + per-port build matrix
      (port × flavor × arch).
- [ ] Signing daemon: holds the production minisign key, signs `.feather` archives +
      `index.toml` per repo channel.
- [ ] `repo.peacock-project.dev` host: serves `<channel>/<flavor>/<arch>/index.toml`
      + archives. Cache + CDN strategy TBD.
- [ ] Channel definitions: `stable`, `testing`, `unstable`. Per-channel signing
      keys. Promotion workflow.
- [ ] Replace `FTR_DEFAULT_PUBKEY` placeholder in `feather/src/verify.c` with the
      production farm key when farm goes live.

### Phase 7+ (compat shims beyond glibc skeleton)

- [ ] `/compat/glibc/` cross-compile for musl-base flavors (Alpine, postmarketOS-musl).
      Current port builds glibc natively; need a per-flavor prebuilt path so musl
      Alpine hosts don't try to bootstrap a glibc cross-toolchain on-device.
- [ ] `/compat/debian/`, `/compat/fedora/`, `/compat/alpine/` — schema reserved,
      `enter-compat` launcher TBD.
- [ ] `/compat/peacock-v1/` (and friends) — populated only when v0.2 introduces an
      ABI break; reserve the layout now.
- [ ] **ATL** (`PeacockProject/atl`): Wine-for-Android-shape syscall translation
      layer. New repo, own roadmap. Reserve `runtime = "compat-android-atl"` in
      port schema today; nothing else.

### Boot stack

- [ ] **MinKernel `kernel_payload_main.c`** is ~2200 LOC after the first modularize
      pass. Lift orchestrator out to `mk_main.c`; leave the rest as focused modules.
- [ ] Kernel-config validation: automated check that PRP-kernel configs disable
      modules, enable required touchscreens, match the documented panel-symbol set.
- [ ] **UART fallback** — the maintainer noted they didn't pack UART for oppo-a16.
      A network-based bootlog over USB-CDC would survive that.
- [ ] mk SCP reinit was added; not yet end-to-end verified working. Manual probe
      against the device + write a follow-up plan if it regresses.
- [ ] mk fastboot menu: confirm the post-touch-fix workflow still uses fastboot
      stage + boot-kernel; document in MinKernel's README.

### Recovery (PRP)

- [ ] **Automated boot health check.** PRP boots → checks last peacockos boot
      completed (marker file + timestamp) → reports back via banner + log. Avoids
      "did the device hang or am I impatient?" UX.
- [ ] Phoenix flag clearing has been fragile in practice — `prp-phoenix-clear`
      ran but the bootloader sometimes re-entered recovery. Investigate whether
      BCB also needs clearing on some MTK variants.
- [ ] `prp-mount-peacock-subparts` shares the canonical `subparts-mount.sh` library
      now (post oppo-a16-ssh-debug merge); add unit-test-style validation against
      sample GPT/loop/dm-linear scenarios on the host so the script doesn't
      regress unnoticed.

### Distribution / install story

- [ ] Self-contained "flash to /sdcard, sideload from recovery" installer image.
      Currently needs PC + fastboot + manual kernel staging; high barrier.
- [ ] Automated USB image creation (`peacock dist usb`?). Same fastboot/sideload
      flow but to USB stick for kiosk recovery.
- [ ] Per-device flashable .zip recoveries — peacock-ports
      `device/oppo-a16-flashable-zip` style.
- [ ] OTA story — feather can upgrade /peacock + /apps; what's the upgrade story
      for the kernel / initramfs themselves? Out-of-band? Via PRP?

### Replace `exec.Command("sudo", ...)` everywhere

- `internal/builder/image_chroot.go` shells out to `sudo` ~30 times.
- Today: requires passwordless sudo for the build user. Brittle + privilege creep.
- Options:
  1. Dedicate a `peacock-builder` group and chown the build root + loop devices.
  2. Carve out a privileged daemon (`peacock-builderd`) that exposes only the
     specific operations needed (mount, losetup, mkfs, install) over a socket.
  3. Run the whole build inside a user namespace (rootless containers).
- Pick one + plan separately.

### Developer experience

- [ ] `peacock fmt` — wrap `gofmt`, `clang-format`, `shfmt`, `taplo fmt` so one
      command formats everything across the project.
- [ ] `peacock environment-check` (or fold into `peacock doctor` — TBD) — sweep
      every cwd-relative assumption and tell the user what's missing.
- [ ] One-shot contributor bootstrap: `curl https://peacock-project.dev/get | sh`
      that clones the right repos, installs prereqs, builds a hello-world device
      image. Today's onboarding takes hours.
- [ ] `peacock build --bisect <port>` — when a build breaks across ~50 ports,
      a bisect would surface the offending port + drop into the chroot. ← Being
      done this round but flagged here for completeness.

### Documentation

- [ ] Architecture overview at the org level — start with `flow/bootflow.md` and
      expand into a full "how everything fits together" landing page. `flow/`
      currently has one file.
- [ ] Per-repo `CONTRIBUTING.md`. Today: no contribution guidelines anywhere.
- [ ] Device port walkthrough — "how to add a new device to peacock-ports". Land
      this round as part of the docs sub-task.
- [ ] `peacock-ports/SCHEMA.md` examples for `layout=app` and `layout=compat`
      beyond `compat/glibc`. Land this round.

### Release / versioning

- [ ] No git tags exist on any repo yet. SemVer + signed tags as soon as
      feather/farm goes live.
- [ ] Per-repo `CHANGELOG.md` (or generated from commits via a `release.sh`).
- [ ] Coordination across feather + Peacock CLI releases: when peacock-mkinitfs
      ships a new feature, Peacock CLI's port pin needs to bump; CI can verify.

### Site / docs hub

- [ ] `PeacockProject/site` has a static-export redesign; content not curated.
      Final landing copy + docs nav structure.
- [ ] `PeacockProject/flow` could become the canonical "story" doc — bootflow,
      meta-distro overview, contributor onboarding, etc. Right now: single
      `bootflow.md`.

### Misc

- [ ] feather: no key rotation story for the production farm pubkey. Plan before
      shipping signed packages widely.
- [ ] feather: `--source` mode for installing without a pre-built archive (AUR-style).
      Plan deferred — depends on whether build farm covers all (port, flavor, arch).
- [ ] feather: package hooks (`post-install.sh` etc.) currently run as root. App
      installs from /apps shouldn't run system-level hooks. Sandbox these or
      restrict by `[install].layout`.
- [ ] peacock-ports: `[build].build_dep_packages` exists alongside `[build].build_deps`
      in five ports for historical reasons. Collapse the duplicate.
- [ ] peacock-ports: `device/linux-xiaomi-daisy-prp` uses a per-build `$PRP_TMP` now;
      apply the same pattern to any other port that grew `/tmp/...` hardcoded paths.
