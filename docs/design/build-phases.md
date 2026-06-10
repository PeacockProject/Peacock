# Design: APKBUILD-style build phases (no more shell in TOML)

Status: **proposed** (spec for review; no code yet)
Scope: `peacock-ports` (lib + every port) and `Peacock` (manifest, build harness)
Supersedes: the `[build].script = """…"""` inline-shell-in-TOML model

## Motivation

Today every port embeds its whole build as a giant `script = """…"""`
string in `package.toml`. That's wrong:

- shell lives inside a TOML string — no syntax highlighting, no
  shellcheck, awkward escaping, heredocs-in-strings;
- every port re-implements the same boilerplate (`tarball=$(ls …);
  tar -xzf … --strip-components=1`, `make install DESTDIR=…`, kernel
  config sanitize + olddefconfig, the cross `MAKE_ARGS` dance);
- there are no defaults to inherit and nothing to override — a one-line
  tweak means copy-pasting the whole script.

Alpine's `APKBUILD`, Arch's `PKGBUILD`, and Gentoo ebuilds all solve this
the same way: **declarative metadata + build logic as shell functions
with sane defaults a port overrides only where it differs.** We adopt
that.

## Model

A port is split:

- **`package.toml`** — metadata + variables only. No `script`.
- **`build.sh`** — defines/overrides phase functions for this port.

Peacock runs a build by assembling, in the chroot:

```
1. env preamble        # vars from package.toml: $pkgname $pkgver $srcdir
                        #   $builddir $pkgdir + CROSS_COMPILE KERNEL_CONFIG …
2. source lib/build/default.sh        # generic default phases + helpers
3. source lib/build/<type>.sh         # the build_type's default phases
4. source ./build.sh                  # the PORT's overrides (if present)
5. run_phases                         # prepare → build → check → package
```

Steps 2–4 are plain `.` (source) in order, so **a function the port
defines redefines the default — last definition wins.** That is the
entire override mechanism; no magic.

A port that needs nothing special ships *no `build.sh` at all* — the
`build_type` defaults build it. Many `base/*` ports collapse to pure
metadata.

## Phase contract

Phases, run in this order by `run_phases`:

| phase       | default does                                  | typical override |
|-------------|-----------------------------------------------|------------------|
| `prepare()` | extract the source tarball into `$builddir`, apply `$patches` | custom unpack, pre-patching |
| `build()`   | per build_type (see below)                    | the actual compile |
| `check()`   | nothing (skipped unless defined)              | run tests |
| `package()` | per build_type (e.g. `make install DESTDIR=$pkgdir`) | stage artifacts |

Environment every phase sees (the contract):

| var | meaning |
|-----|---------|
| `$pkgname` `$pkgver` | from `[package]` |
| `$srcdir`   | working dir holding the downloaded tarball(s) + port files |
| `$builddir` | extracted source root (default `$srcdir`) where build runs |
| `$pkgdir`   | stage dir that becomes the artifact (the old `stage/`) |
| `$jobs`     | parallelism (`PEACOCK_JOBS`) |
| `CROSS_COMPILE` `ARCH` | cross build (from capabilities/triple) |
| `KERNEL_CONFIG` `PRP_KERNEL_CONFIG` | kernel config file names |
| `PATH` `LD_LIBRARY_PATH` … | build-dep package wiring (unchanged) |

`$pkgdir` is the renamed `stage/`; `PackageArtifact` already tars it.

## Default phase library (in peacock-ports)

Lives in `peacock-ports/lib/build/`, shipped as data (editable without
rebuilding Peacock — the abuild model, and the meta-distro "logic in
ports" philosophy). The Go harness only orchestrates.

- `lib/build/default.sh` — `prepare()` (extract + patch), no-op
  `build()`/`check()`/`package()`, and helpers (`peacock_extract`,
  `peacock_msg`, `apply_patches`).
- `lib/build/<type>.sh` — the per-type defaults below.

This is **one shared library**, edited in one place. It is never copied
into a port's source dir — a port dir holds only its `package.toml`,
optional `build.sh`, and config files. The shared `lib/` is bind-mounted
into the throwaway chroot build sandbox at build time and sourced from
there.

## Build types

`[build].type` selects the default phase set. Default: `raw`.

- **`raw`** — `prepare()` extracts; `build()`/`package()` are no-ops the
  port must define. For fully custom ports.
- **`make`** — `build()`: `make $MAKE_ARGS -j$jobs`; `package()`:
  `make install DESTDIR=$pkgdir PREFIX=${prefix:-/usr}`.
- **`autotools`** — `build()`: `./configure --prefix=${prefix:-/usr} …
  && make -j$jobs`; `package()`: `make install DESTDIR=$pkgdir`.
- **`kernel`** — the interesting one. Provides:
  - `prepare()` → extract, then `kernel_apply_config` (copy
    `$KERNEL_CONFIG`, sanitize compiler-capability symbols,
    `olddefconfig`), then call the hook `kernel_config_tweaks` (default
    no-op) and `kernel_dts_tweaks` (default no-op).
  - `build()` → `make Image.gz` + the board DTB + modules.
  - `package()` → assemble `zImage` (Image.gz + dtb), stage modules.
  - **PRP second pass**: when `$PRP_KERNEL_CONFIG` is set, after the full
    pass it re-runs prepare/build with the PRP config and the
    `kernel_prp_tweaks` hook, staging `zImage-prp`.

  A device kernel port's `build.sh` then only defines the **hooks** that
  differ — `kernel_config_tweaks`, `kernel_dts_tweaks`,
  `kernel_prp_tweaks`, `kernel_dtb_target` — not the whole pipeline. The
  current ~250-line daisy two-pass script becomes the kernel-type default
  plus ~40 lines of daisy hooks.

New build types are new `lib/build/<type>.sh` files; no Go change.

## Harness changes (Peacock, Go)

`BuildPackageInChroot` today writes `pkg.Build.Script` to
`peacock-build.sh` and runs it. New flow:

1. Resolve `build_type` (default `raw`).
2. Bind-mount the one shared `peacock-ports/lib/build/` into the chroot
   build sandbox (read-only) at a fixed path, e.g. `/peacock-buildlib`.
   Nothing is copied; the same single library serves every build.
3. Generate `peacock-build.sh` = the env preamble + the source-and-run
   driver:
   ```sh
   . /peacock-buildlib/default.sh
   . /peacock-buildlib/<type>.sh
   [ -f ./build.sh ] && . ./build.sh
   run_phases            # defined in default.sh
   ```
4. Run it in the chroot exactly as today (same env injection, same
   sudo/qemu paths).

`run_phases` calls `prepare`, `build`, `check` (if defined), `package`
in order with `set -e`.

## Manifest changes (`internal/manifest`)

- add `[build].type` (`BuildType string`, default `"raw"`).
- add optional `[build].patches`, `[build].prefix` (consumed by default
  phases).
- **remove `Script string`** — `script` is gone after the sweep. The
  harness errors if a port has neither a recognized type's defaults nor a
  `build.sh` that defines `build()`.

## Migration — big-bang sweep

Every port converts in one pass:

1. Land `lib/build/{default,raw,make,autotools,kernel}.sh` + the harness
   change + manifest `type`, remove `Script`.
2. For each port: move the `script` body into a `build.sh` as phase
   functions, set `[build].type`, delete `script`. Simple ports
   (extract + configure/make/install) become **metadata + `type` only,
   no `build.sh`**. Complex ports (kernels, lk2nd, PRP recovery) get a
   `build.sh` with the type's hooks/overrides.
3. `phase1-verify` + a new check: every port either resolves to a type
   with a default `build()`/`package()` or ships a `build.sh` defining
   them.
4. Rebuild the daisy kernel + a couple of base ports end-to-end to prove
   each type.

Order to convert: base/* autotools+make ports first (they mostly vanish
into defaults and prove the common path), then the kernels, then
lk2nd/PRP.

## Examples

**Before** (`base/util-linux/package.toml`, abridged):
```toml
[build]
script = """
tarball="$(ls -1 util-linux-*.tar.gz | head -n1)"
tar -xzf "$tarball" --strip-components=1
./configure --prefix=/usr …
make
make install DESTDIR=stage
"""
```
**After**:
```toml
[build]
type = "autotools"
configure_args = "--disable-nls --without-systemd"
# no build.sh — the autotools type builds it
```

**Daisy kernel after** — `package.toml` sets `type = "kernel"`,
`kernel_config`, `prp_kernel_config`; `build.sh`:
```sh
kernel_dtb_target() { echo "qcom/msm8953-xiaomi-daisy.dtb"; }
kernel_config_tweaks() {
  scripts/config --disable ARM64_PTR_AUTH …
  scripts/config --enable DRM_MSM_MDSS …
}
kernel_dts_tweaks() { …panel + touch sed… }
kernel_prp_tweaks() { …MODULES=n, trim lists, display policy… }
```
The extract / sanitize / olddefconfig / make / zImage assembly / two-pass
all come from `lib/build/kernel.sh`.

## Non-goals

- Subpackage splitting (abuild's `subpackages`) — out of scope; Peacock's
  `[install].layout` handles overlay placement.
- A `check()`/test gate requirement — optional, off by default.
- Changing the download/cache/chroot machinery — only what runs *inside*
  the build dir changes.

## Open questions

1. ~~Lib delivery~~ **Resolved:** bind-mount the one shared
   `peacock-ports/lib/build/` read-only into the sandbox at
   `/peacock-buildlib`. Nothing is copied into package or build dirs; a
   single library serves every build.
2. `build_type` default: `raw` (explicit, a port must opt into a type) vs
   inferring from declared fields. Leaning explicit `raw`.
3. Do bootloaders (lk2nd) get a `bootloader`/`make`-ish type, or stay
   `raw` with a full `build()`? Leaning `raw` for now; revisit if a
   pattern emerges across lk2nd/minkernel.
4. Keep a thin `script` escape hatch (a `raw` port whose `build.sh`
   defines one `build()` that is the old script) during the sweep, or
   hard-remove? The sweep removes `script`; the escape hatch is just
   "a `raw` port with a `build.sh`".
