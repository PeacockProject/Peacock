// Package host's chroot.go scaffolds the pmbootstrap-style
// chroot-per-target build strategy. The shape is:
//
//   ~/.local/var/peacock/host-chroots/<flavor>/
//
// When the user opts into `--use-host-chroot <flavor>` (or sets
// PEACOCK_HOST_CHROOT=<flavor>), Peacock first ensures this directory
// exists by either reusing it (idempotent) or downloading a minimal
// rootfs tarball, untarring it, and applying basic mounts.
//
// The host's pacman/apt/apk/qemu/cross-gcc requirements collapse to
// just `chroot`, `tar`, `curl` — everything else lives inside the
// host chroot and gets installed there once at first run.
//
// Status (v0): the directory layout is fixed and EnsureHostChroot
// returns a clear "not yet implemented" error. The actual download +
// extract + first-time setup is captured in BACKLOG.md and lands in a
// follow-up. cmd/peacock/build.go's --use-host-chroot flag wires
// through to here so the user-facing flag is real today.
package host

import (
	"fmt"
	"os"
	"path/filepath"
)

// SupportedHostChrootFlavors is the closed set we know how to (or
// will know how to) bootstrap. Matches config.ValidFlavors but
// duplicated here to avoid the import cycle.
var SupportedHostChrootFlavors = []string{"arch", "debian", "alpine"}

// IsSupportedHostChrootFlavor reports whether `flavor` is a flavor
// EnsureHostChroot can bootstrap.
func IsSupportedHostChrootFlavor(flavor string) bool {
	for _, f := range SupportedHostChrootFlavors {
		if f == flavor {
			return true
		}
	}
	return false
}

// Tarball URLs per flavor. Constants live here so the BACKLOG entry
// has one obvious place to point at when the implementation lands.
//
// Arch's `latest` resolves to a dated archlinux-bootstrap-<date>-x86_64.tar.gz;
// the implementation will need to fetch the directory listing and pick
// the newest. For now these constants stand as documentation.
const (
	ArchBootstrapURL   = "https://archive.archlinux.org/iso/latest/archlinux-bootstrap-x86_64.tar.gz"
	DebianRootfsURL    = "https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.tar.xz"
	AlpineMinirootURL  = "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.0-x86_64.tar.gz"
)

// TarballURL returns the canonical bootstrap tarball URL for a flavor.
// Returns "" for unknown flavors so callers can produce a clear error.
func TarballURL(flavor string) string {
	switch flavor {
	case "arch":
		return ArchBootstrapURL
	case "debian":
		return DebianRootfsURL
	case "alpine":
		return AlpineMinirootURL
	default:
		return ""
	}
}

// HostChrootBaseDir is the parent under which per-flavor chroots live.
// Resolved against $HOME so multiple Peacock checkouts share a single
// chroot per flavor.
func HostChrootBaseDir() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("host: cannot resolve $HOME: %w", err)
	}
	return filepath.Join(home, ".local", "var", "peacock", "host-chroots"), nil
}

// HostChrootRoot returns the path where the chroot for `flavor` lives
// (or would live if not yet bootstrapped). Idempotent: no side effects.
func HostChrootRoot(flavor string) (string, error) {
	if !IsSupportedHostChrootFlavor(flavor) {
		return "", fmt.Errorf("host: unsupported host-chroot flavor %q (supported: %v)", flavor, SupportedHostChrootFlavors)
	}
	base, err := HostChrootBaseDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(base, flavor), nil
}

// EnsureHostChroot is the v0 entrypoint: idempotently materialize a
// host chroot for `flavor` and return its path. When the chroot
// already exists, no work is done.
//
// v0: the download+extract+first-time-setup steps are still TODO. We
// return a clear "not yet implemented" error so the build path that
// calls into here fails loudly rather than silently falling back to
// the host's own tools (which would defeat the point of opting into
// host-chroot mode).
//
// BACKLOG.md tracks the rest:
//   - fetch `latest` directory listing for arch and pick the newest dated tarball.
//   - download with progress + checksum verification.
//   - bind-mount /dev, /proc, /sys (already covered by internal/chroot.MountWithSudo).
//   - first-time apt/pacman/apk install of the build toolchain inside the chroot.
//   - integrate with runner.SetExecPrefix or equivalent so RunCmd routes through the chroot.
func EnsureHostChroot(flavor string) (string, error) {
	root, err := HostChrootRoot(flavor)
	if err != nil {
		return "", err
	}

	// Idempotency check: if the rootfs already has a recognizable
	// init.d / etc / usr structure, we treat it as already
	// bootstrapped. The fuller "matches the flavor we expect" check
	// (e.g. /etc/alpine-release vs /etc/debian_version) lands with the
	// real implementation.
	if isProbablyBootstrapped(root) {
		return root, nil
	}

	url := TarballURL(flavor)
	if url == "" {
		return "", fmt.Errorf("host: no bootstrap tarball URL known for flavor %q", flavor)
	}

	return root, fmt.Errorf("host: --use-host-chroot=%s not yet implemented (would bootstrap from %s into %s); see BACKLOG.md", flavor, url, root)
}

// isProbablyBootstrapped is a cheap "does this look like a populated
// rootfs?" check. We don't validate the flavor here — just the shape.
func isProbablyBootstrapped(root string) bool {
	for _, marker := range []string{
		filepath.Join(root, "etc"),
		filepath.Join(root, "usr", "bin"),
		filepath.Join(root, "bin"),
	} {
		if _, err := os.Stat(marker); err == nil {
			return true
		}
	}
	return false
}
