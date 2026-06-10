# Design: build capabilities + toolchain resolution

Status: **implemented** — peacock-ports `toolchains.toml` + `internal/toolchain`
resolver + manifest `capabilities`/`triple` fields; daisy kernel ports migrated.
The `[capabilities.<cap>.<mode>.<flavor>]` nested-table form below is the
shipped shape (the earlier inline `{ unsupported = … }` was illustrative).
Scope: `peacock-ports` (schema + data) and `Peacock` (manifest, resolver)
Supersedes: the `gcc-<target_arch>` alias-injection shim
(peacock-ports `02cf905`, Peacock `e93dfb5`)

## Context

Kernel ports cross-compile for ARM. They used to hardcode the cross
toolchain in `build_deps`:

```toml
build_deps = [..., "aarch64-linux-gnu-binutils", "aarch64-linux-gnu-gcc"]
```

Those are **Arch-x86 cross-package names** baked into a port. They break
in any chroot that isn't Arch-x86: wrong on Debian/Alpine (different
names), wrong in qemu mode (you want *native* gcc in the emulated target
chroot, not a cross compiler), and absent from the archlinuxarm repos
(which is what blew up the xiaomi-daisy + PRP kernel builds).

The current shim improved the port side — ports now declare
`target_arch = "aarch64"` and Peacock injects an abstract `gcc-<arch>`
token that the flat `flavors/*/aliases.toml` table expands per distro.
But the shim has four smells:

1. **Stringly-typed.** `gcc-<arch>` is a string spliced into `build_deps`
   and resolved by a flat string→string map. A missing entry silently
   falls through to the literal token and fails at `pacman -S` time.
2. **Conflated concerns.** Simple renames (`python→python3`) and
   arch×mode-parameterized toolchain expansion live in the same flat map.
3. **Three arch fields that must agree, unenforced** — `cross_compile`
   prefix, `target_arch`, the injected token. The samsung `arm-eabi-`
   mismatch is this seam showing.
4. **Only gcc is abstracted.** A cross pkg-config, a target sysroot, or
   qemu helpers have no equivalent — gcc got special-cased.

This design replaces the shim with a model that scales to N distros × M
arches × build modes and beyond gcc, without a file-per-package
explosion.

## Model: capabilities vs build_deps

A port splits its build-time needs into two kinds:

```toml
[build]
build_deps   = ["bc", "dtc", "openssl"]   # plain packages → rename table (flavors/)
capabilities = ["c-toolchain"]            # abstract needs → resolved by mode × arch × flavor
target_arch  = "aarch64"
# triple = "arm-eabi"                      # optional per-port override (see below)
```

- **`build_deps`** — unchanged from today. Flat per-distro renames via
  `flavors/<flavor>/aliases.toml`. Right tool for renames; stays.
- **`capabilities`** — abstract requirements resolved knowing the build
  **mode** and **target triple**. The resolver picks the native vs cross
  package set and templates the triple in.

Both kinds install into the same build chroot for a given mode (host-arch
chroot for cross, target-arch chroot for qemu/native), so composition is
trivial — no split install path.

## Principle: lean on the distro's own packages

Where a distro blesses a meta-package or group for a capability, **use
it** rather than hand-enumerating its members. Distros already solved
"what's the toolchain for arch X"; depending on their package means the
distro maintains its contents and we never drift.

| distro | native C toolchain | cross C toolchain (aarch64) |
|---|---|---|
| Debian/Ubuntu | `build-essential` (meta) | `crossbuild-essential-arm64` (meta) |
| Arch | `base-devel` (group) | *no meta* → enumerate `aarch64-linux-gnu-gcc aarch64-linux-gnu-binutils` |
| Alpine | `build-base` (meta) | none official → `unsupported` |
| Fedora (future) | `@development-tools` | `gcc-aarch64-linux-gnu` |

Rule: **prefer the distro's meta/group; enumerate individual packages
only as a fallback** where the distro has none (Arch cross). This trims
what we own to "the distro's blessed package name" plus the arch-naming
tables below.

## Data: one file, triple- and distro-arch-keyed

The GNU **triple** is the canonical key for `CROSS_COMPILE` and for
distros whose package names are triple-based (Arch). But distro
meta-packages use the **distro's own arch name** (`arm64`, `armhf`), not
the triple — so we also carry a per-distro arch table. A single
`peacock-ports/toolchains.toml`:

```toml
# GNU triple → CROSS_COMPILE + triple-based package names (Arch, Fedora).
[triples]
aarch64 = "aarch64-linux-gnu"
armv7h  = "arm-linux-gnueabihf"
x86_64  = "x86_64-pc-linux-gnu"

# Debian/Ubuntu arch tags → their meta-packages (crossbuild-essential-*).
[debarch]
aarch64 = "arm64"
armv7h  = "armhf"

# C/C++ toolchain capability. Each [capabilities.<cap>.<mode>.<flavor>]
# cell is a table with either `packages = [...]` or `unsupported = "..."`.
[capabilities.c-toolchain.native.arch]    # qemu/native: native toolchain, TARGET chroot
packages = ["base-devel"]
[capabilities.c-toolchain.native.debian]
packages = ["build-essential"]
[capabilities.c-toolchain.native.alpine]
packages = ["build-base"]

[capabilities.c-toolchain.cross.arch]     # cross: distro's blessed package, HOST chroot
packages = ["{triple}-gcc", "{triple}-binutils"]   # no meta on Arch → enumerate
[capabilities.c-toolchain.cross.debian]
packages = ["crossbuild-essential-{debarch}"]      # lean on Debian's meta
[capabilities.c-toolchain.cross.alpine]
unsupported = "Alpine ships no linux-gnu cross toolchain; use use_qemu=true"
```

Substitution tokens available in any package string: `{triple}`
(from `[triples]`) and `{debarch}` (from `[debarch]`). An entry uses
whichever its distro's package naming wants.

- New distro → add a key to each block (+ an arch-name table if its
  packages aren't triple-based).
- New arch → one line in `[triples]` (and `[debarch]` if Debian-family
  support is wanted).
- New capability → a new `[<name>.native]` / `[<name>.cross]` section
  (e.g. `[rust-toolchain]`, a future `[sysroot]`).
- The real capability count stays tiny (C, maybe Rust, sysroot), so this
  never becomes a directory — it's one file. This is the answer to the
  earlier "no file-per-package explosion" constraint.

## cross_compile is derived, not declared

`CROSS_COMPILE` is computed from the triple, so the make prefix and the
toolchain packages can never disagree:

```
CROSS_COMPILE = triples[target_arch] + "-"     # e.g. "aarch64-linux-gnu-"
```

- Removes `cross_compile` from the kernel ports.
- The per-port `triple` override (e.g. samsung `arm-eabi`) flows into
  *both* the package resolution and the derived `CROSS_COMPILE`, so the
  oddball case stays consistent by construction.
- An explicit `[build].cross_compile` remains honored as a final escape
  hatch (wins over the derivation) for a port that truly needs it.

## Resolver contract (Go)

Lives in `internal/builder` (or a new `internal/toolchain`), consumed by
`BuildPackageInChroot` where `ResolveBuildDeps` is called today.

```
ResolveCapabilities(caps []string, targetArch, triple, flavor string, mode Mode)
    (pkgs []string, crossCompile string, err error)
```

Inputs:
- `caps` — the port's `capabilities`.
- `targetArch` — the port's `target_arch` (may be empty for host-native tools).
- `triple` — the port's `triple` override, else "".
- `flavor` — active base distro (arch|debian|alpine).
- `mode` — `cross` or `native`, already resolved upstream from
  `use_qemu` + (host vs target) in `resolveBuildOptions`.

Algorithm, per capability:
1. Resolve triple: port override → `triples[targetArch]` → if cross and
   still empty: **error** (`no triple for arch %q`).
2. Pick block: `mode==cross` → `[cap.cross][flavor]`; else `[cap.native][flavor]`.
3. If the block is `{ unsupported = "msg" }` → **error** with msg.
4. If no block for `flavor` → **error** (`capability %q undefined for flavor %q`).
5. Substitute `{triple}` (from `[triples]`) and `{debarch}` (from
   `[debarch]`) in each package string; append to `pkgs`. A package
   string that references `{debarch}` for an arch missing from the
   `[debarch]` table is a **plan-time error** (`no debarch for arch %q`),
   same fail-fast rule as the triple.

`crossCompile` = `triples[targetArch] + "-"` when `mode==cross` (unless the
port set `[build].cross_compile` explicitly). Returned so the build step
threads it into `CROSS_COMPILE`.

**Fail-fast:** all the error cases above fire at plan time (before any
chroot work / package download), not mid-`pacman`. That's the key
correctness win over the shim.

## Manifest changes (`internal/manifest`)

Add to the `[build]` struct:
- `Capabilities []string` (`toml:"capabilities"`)
- `Triple string` (`toml:"triple"`) — per-port override
- keep `TargetArch` (already added)
- `CrossCompile` stays (now an override, not the primary source)

## Migration

1. Land `toolchains.toml` + the resolver + manifest fields.
2. Convert toolchain-needing ports:
   - `linux-xiaomi-daisy`, `linux-xiaomi-daisy-prp`: drop `cross_compile`
     + the `gcc-aarch64` reliance; add `capabilities = ["c-toolchain"]`
     (they already have `target_arch = "aarch64"`).
   - `linux-samsung-jflte`: `target_arch = "armv7h"`,
     `capabilities = ["c-toolchain"]`, `triple = "arm-eabi"` (override).
   - `linux-oppo-a16`: `use_qemu = true` → native; add
     `capabilities = ["c-toolchain"]`, no triple needed.
3. Remove the `gcc-<arch>` shim entries from `flavors/*/aliases.toml`
   once no port references them. The plain renames in `flavors/` stay.
4. Round-trip + `phase1-verify` all manifests; rebuild a daisy + oppo
   image to confirm cross and qemu paths both resolve.

## Non-goals (this round)

- **Feather-as-installer.** When a distro can't provide a toolchain
  natively (Alpine cross), we mark it `unsupported` and fail fast. A
  future `provider = "feather:<pkg>"` resolution type is the natural
  extension but is out of scope.
- **Replacing `flavors/` renames.** The flat alias table is the right
  tool for `python→python3`; untouched.
- **Non-C toolchains.** `[rust-toolchain]` etc. are shape-compatible but
  not defined until a port needs them.

## Open questions

1. Resolver home: extend `internal/builder` or a new `internal/toolchain`
   package? (Leaning `internal/toolchain` — it's a distinct concern and
   keeps `builder` from growing.)
2. `toolchains.toml` location: repo root of peacock-ports (alongside
   `flavors/`) vs `flavors/toolchains.toml`. (Leaning repo root — it's
   cross-flavor, not per-flavor.)
3. Should `capabilities` default to `["c-toolchain"]` for any port that
   has a `script` + `source` (i.e. compiles something), so most ports
   don't declare it? Or keep it explicit? (Leaning explicit — implicit
   toolchain injection is how we got the original mess.)
4. Validation tool: extend `peacock-ports/tools/phase1-verify.py` (or a
   new linter) to check every `capabilities` entry resolves for every
   declared `flavor` × `target_arch`, so a typo fails CI-style locally
   rather than at build time.
