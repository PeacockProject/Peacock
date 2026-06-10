# Design: build for feather (.feather packages, ftr-installed)

Status: **proposed** (spec for review; no code yet)
Scope: `feather` (install/repo), `Peacock` (PackageArtifact + consumption),
`PRP`, and every port indirectly
Supersedes: pacman-style `.pkg.tar.gz` build outputs + the build-dir /
cache walking that PRP and build-deps rely on

## Motivation

PeacockOS has its own package manager — **feather (`ftr`)**. The build
should produce **feather packages** and consumers should **`ftr install`**
them, instead of emitting pacman-style `.pkg.tar.gz` and reaching into
build dirs / caches by hand. This makes the build output a real repo, and
collapses three ad-hoc consumption paths (PRP `resolve_kernel_image`,
build-dep `findPaths` staging, kernel artifact extraction) into one:
`ftr install`.

## What feather actually is (verified)

- **`.feather` archive** = a `tar.gz` containing:
  - `manifest.toml` at the root — `[package].name`, `[package].version`,
    `[install].layout` (peacock | app | compat | **system**), optional
    `[provides]` / `[conflicts]` capability tables, optional `runtime`.
  - `files/` — the payload tree, copied into `<prefix>/`.
  - `hooks/` (optional) — `pre-install.sh` / `post-install.sh`.
- **`ftr install`** takes a local `.feather` path or a bare name resolved
  against synced repos; extracts, parses manifest, runs hooks, copies
  `files/` into the layout's prefix, records into its DB.
- **Prefix per layout**: peacock → `/peacock`, app → `/apps/<name>`,
  compat → `/compat/<runtime>`. Overridable via `--peacock-prefix` etc.
  and `FEATHER_PREFIX`.
- **Repo**: each repo has an `index.toml`; transports are `file://`
  (copy) and `http(s)://` (curl). `ftr sync` refreshes indexes.
- **Signing**: `ftr-sign` + `verify.c`; `--allow-unsigned` for local
  archives only.

## The hard dependency: feather can't install `system` layout

`install.c` resolves only peacock/app/compat; **everything else hits
`default: "layout … not yet supported in phase 4"` and aborts.** Our
build packages (toolchains, kernels, busybox, util-linux, …) are
`layout = system` → `/usr`. So none of them are ftr-installable today.

This must be closed first. Options:

- **A. Teach feather `system` layout** → install `files/` into
  `<root>/usr` (or `<root>/`), with a `--root <dir>` (or `FEATHER_PREFIX`)
  so the build can install a package into a build chroot rather than the
  host. *Recommended* — it's the missing case, ~one switch arm + a root
  option, and it's needed regardless.
- B. Re-home build/runtime packages to `peacock` layout (`/peacock`).
  Avoids the feather change but rewrites every port's layout and the
  on-disk FS contract; bigger blast radius.

Recommendation: **A**. Small, contained feather change; unblocks
everything below.

## Package format change (Peacock `PackageArtifact`)

Emit `<name>-<ver>-<rel>-<arch>.feather` into `packages/<arch>/` (the
store we just built), instead of `.pkg.tar.gz`:

- write `manifest.toml` (from the port's `[package]` + `[install]` +
  provides/conflicts) at the archive root;
- move the staged tree under `files/` (today's `stage/` becomes
  `files/`);
- carry `hooks/` if the port ships install hooks;
- `tar -czf` the lot.

`.PKGINFO` and the pacman naming go away. `cachedArtifactPath` /
`FindCachedPackageArtifact` switch to `.feather`.

## Consumption model (the point of all this)

Replace the three hand-rolled paths with `ftr install`:

- **Build-deps** (`build_dep_packages`): instead of `tar -xzf` into
  `/peacock` + `findPaths` + PATH/LD wiring, run
  `ftr install <pkg>.feather --root <build-chroot>` (or `ftr install
  <name>` against the local `packages/<arch>/` repo). The toolchain/tool
  lands at its real prefix in the chroot; PATH picks it up normally.
- **PRP**: the PRP build root runs `ftr install linux-<dev>`; the kernel
  package's `files/` (zImage, zImage-prp, dtbs) land at a known installed
  path. PRP reads from **where ftr installed it** — no
  `resolve_kernel_image` cache/build-dir walking.
- **`resolve_kernel_image`** shrinks to "the installed kernel path", or
  is retired.

## #2 — delete build dirs after packaging

Once consumers `ftr install` (above), nothing reads the build tree after
it's packaged. So `BuildPackageInChroot` removes
`build-chroot/<arch>/build/<pkg>-<ver>-<arch>/` right after
`PackageArtifact` succeeds. Gated strictly on the consumption switch —
deleting earlier breaks PRP + build-deps today.

## Repo index

Generate `packages/<arch>/index.toml` after a build (name, version, arch,
archive filename, sha256, deps/provides) so `ftr sync` + `ftr install
<name>` work. Build-deps can also install by direct path and skip sync.

## Migration phases

1. **feather**: add `system` layout install + `--root`/`FEATHER_PREFIX`
   targeting (dependency A). Ship + tag.
2. **Peacock**: `PackageArtifact` emits `.feather`; `manifest.toml`
   writer; `cachedArtifactPath`/`FindCachedPackageArtifact` →`.feather`.
3. **Repo index** generation for `packages/<arch>/`.
4. **Consumption**: build-deps + PRP switch to `ftr install`; retire
   `findPaths`/`resolve_kernel_image` cache-walking.
5. **#2**: delete build dirs after packaging.
6. Drop the `.pkg.tar.gz` path + the PRP `common.sh` cache globs.

Each phase is shippable; the order is forced by the dependency chain
(feather → format → consumption → cleanup).

## Open questions

1. `--root` vs running `ftr` *inside* the chroot. A `--root` flag is
   cleaner (no ftr binary needed in every chroot) but touches more of
   feather's path handling. Leaning `--root`.
2. Signing build packages: sign with a project key in `ftr-sign`, or
   `--allow-unsigned` for the local build repo? Leaning unsigned for the
   local `packages/` repo, sign only for published/remote repos.
3. Does feather need a `db`-per-root, so installing into a build chroot
   doesn't pollute the host feather DB? (Likely yes — the DB path must
   follow `--root`.)
4. Kernel package layout: `system` (/usr) puts zImage where? The port
   stages `zImage`/`zImage-prp` at the package root today — decide their
   canonical installed path so PRP knows where to look (e.g.
   `/usr/share/peacock/<dev>/zImage-prp` or `/boot`).
