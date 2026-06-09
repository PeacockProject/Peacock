# Peacock

The Peacock CLI: a Go tool that turns a `peacock-ports/` tree into a bootable
mobile-device image. It bootstraps a base-distro chroot (Arch / Debian /
Alpine), builds each port inside that chroot, assembles an initramfs via
`peacock-mkinitfs`, stages a rootfs, and emits the per-device boot artifacts
(boot.img, mk chainloader, lk2nd image) ready to flash with `fastboot`.

This is the build front-end for the wider Peacock meta-distro project. It is
not the on-device package manager (that's [feather](../feather/) — `ftr`); it
is not the initramfs builder (that's [peacock-mkinitfs](../peacock-mkinitfs/));
it is not the package source (that's [peacock-ports](../peacock-ports/)).
Peacock orchestrates all of them.

## How it fits in

```
  peacock-ports/         source manifests + flavor alias tables
        │
        ▼
  Peacock (this repo)    chroot bootstrap → port builds → rootfs → boot.img
        │       │
        │       └──► peacock-mkinitfs      builds /init + cpio.gz
        │       └──► MinKernel / lk2nd     device-side bootloader staging
        ▼
   boot.img + rootfs ────────────► fastboot stage / flash → device
                                                              │
                                                              ▼
                                                          feather (ftr)
                                                          manages /peacock,
                                                          /apps, /compat,
                                                          /data on-device
```

Sibling repos under `PeacockProject/`:

- **[peacock-ports](../peacock-ports/)** — `package.toml` manifests for every
  base port, device port, and compat shim. See `peacock-ports/SCHEMA.md`.
- **[peacock-mkinitfs](../peacock-mkinitfs/)** — standalone CLI that builds the
  initramfs `cpio.gz`. Invoked by `peacock build` via `exec.Command`.
- **[feather](../feather/)** — `ftr`, the on-device package manager for the
  Peacock platform layer (`/peacock`), user apps (`/apps`), compat shims
  (`/compat`), and per-app state (`/data`).
- **MinKernel** — MTK chainloader (`mk`) that stages the real kernel from a
  recovery boot.img. Built via `peacock-ports/device/minkernel-<device>/`.
- **lk2nd_peacock** — Qualcomm secondary bootloader fork. Built via
  `peacock-ports/device/lk2nd-<device>/`.
- **PRP** — Peacock Recovery Partition. A minimal recovery rootfs used to
  stage kernels, debug failed boots, and read `ramoops`.

The meta-distro architecture (`/peacock`, `/apps`, `/compat`, `/data` as
overlay namespaces on top of a base distro at `/usr`) is summarized in
`peacock-ports/SCHEMA.md` and tracked in `BACKLOG.md`.

## Quick start

Host prerequisites (Arch host today; Debian and Alpine are in flight):

- Go 1.22+
- `pacman`, `pacstrap`, `arch-install-scripts`
- `mkbootimg`, `cpio`, `mtools`, `dosfstools`, `e2fsprogs`
- `qemu-user-static-binfmt` for cross-arch ports
- Passwordless `sudo` (used by chroot + loop-device steps; tracked as
  tech-debt in `BACKLOG.md`)
- `fastboot` on the host for staging / flashing

A `peacock doctor` subcommand to sweep these prereqs is in flight — until it
lands, see the host checks scattered through `internal/host/`.

```sh
git clone --recurse-submodules \
  https://github.com/PeacockProject/Peacock.git
cd Peacock
go build -o peacock ./cmd/peacock

# Build for a device that already has a port in peacock-ports/device/.
./peacock build --device oppo-a16 --init openrc

# Or just rebuild one port end-to-end and stop.
./peacock build-packages --device oppo-a16 -p linux-oppo-a16
```

Outputs land in `out/`. For oppo-a16 you get `boot.img`,
`mk-oppo-a16-boot.img`, and a rootfs tarball; flash the mk image to your
recovery slot, fastboot-stage the kernel boot.img, and you're up.

For non-Arch hosts: `peacock build --flavor debian --device oppo-a16` uses
`debootstrap` instead of `pacstrap`; `--flavor alpine` uses `apk`. These
paths are real but newer than the Arch path; expect rougher edges.

## Project layout

```
Peacock/
├── cmd/peacock/        CLI entrypoints: build, build-packages, chroot, flavor, init
├── internal/
│   ├── builder/        chroot orchestration, port builds, image assembly
│   ├── chroot/         low-level chroot lifecycle (mount, bind, unwind)
│   ├── config/         viper-backed config + flavor keys
│   ├── feather/        wrapper around `ftr` (phase 4+; stub today)
│   ├── host/           host-prereq checks
│   ├── image/          boot.img + rootfs assembly
│   ├── manifest/       package.toml parser + ResolvedLayout / ResolvedPrefix
│   ├── pacman/         Arch flavor: pacstrap + pacman in chroot
│   ├── apt/            Debian flavor: debootstrap + apt in chroot
│   ├── apk/            Alpine flavor: apk-tools in chroot
│   ├── runner/         exec helpers + sudo wrappers
│   └── userland/       rootfs population, init system wiring
├── mk/                 vendored MinKernel artifacts (legacy; being replaced
│                       by peacock-ports/device/minkernel-<device>)
├── peacock-ports → ../peacock-ports/ (submodule / sibling clone symlink)
├── tools/              qemu helpers for rootfs smoke tests
├── assets/             baked-in images (conspiracy.png)
├── task.md             per-device porting checklist
└── BACKLOG.md          cross-repo improvement backlog
```

## Where to go next

- **Porting a new device** —
  [`peacock-ports/docs/adding-a-device.md`](../peacock-ports/docs/adding-a-device.md).
- **Manifest schema reference** — `peacock-ports/SCHEMA.md`.
- **Per-port porting checklist** — `task.md` (this repo).
- **Cross-repo improvement backlog** — `BACKLOG.md` (this repo).
- **On-device package manager design** —
  [`feather/README.md`](../feather/README.md).
- **Initramfs build pipeline** —
  [`peacock-mkinitfs/README.md`](../peacock-mkinitfs/README.md).

## License

TODO. No `LICENSE` file ships in this repo today; treat the source as
all-rights-reserved until the project picks a license (most likely GPL-3.0 to
match feather).

## Contributing

TODO. No `CONTRIBUTING.md` yet. Until one lands:

- Commits use a `<area>: <subject>` prefix (`peacock:`, `docs:`, `tools:`).
- Don't push to `master` directly while the project is pre-release; open a
  PR and tag the maintainer.
- `gofmt`, `go vet`, `go test ./...` should all pass.
- Cross-repo changes (peacock-ports schema bumps, feather surface changes)
  should land coordinated commits in each repo.
