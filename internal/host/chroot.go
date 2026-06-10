// Package host's chroot.go scaffolds the pmbootstrap-style
// chroot-per-target build strategy. The shape is:
//
//	~/.local/var/peacock/host-chroots/<flavor>/
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
// EnsureHostChroot is real end-to-end: download → sha256-verify (fail
// closed) → extract → first-time toolchain install → bind-mounts.
// arch + alpine bootstrap from stable upstream URLs; debian has no stable
// rootfs tarball, so it goes through $PEACOCK_DEBIAN_ROOTFS_URL or a
// clear, actionable error. cmd/peacock/build.go's --use-host-chroot flag
// wires through to here.
package host

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"peacock/internal/runner"
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
// Arch publishes a STABLE-named bootstrap tarball under the geo mirror's
// iso/latest/ that resolves to the newest dated build, so no listing
// scrape is needed for the happy path. Note the compression is .tar.zst.
//
// Debian deliberately has NO constant: it does not publish a simple
// stable rootfs tarball (the cloud genericcloud .tar.xz is a disk IMAGE,
// not a chroot). The Debian flavor goes through an env-or-clear-error
// path; see resolveTarballURL and $PEACOCK_DEBIAN_ROOTFS_URL.
const (
	ArchBootstrapURL  = "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
	AlpineMinirootURL = "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.0-x86_64.tar.gz"
)

// Debian escape-hatch environment variables. Debian has no stable rootfs
// tarball, so the user must point Peacock at one explicitly:
//   - PEACOCK_DEBIAN_ROOTFS_URL: a flat .tar.xz/.tar.gz Debian rootfs
//     (e.g. a debuerreotype / docker-debian-artifacts rootfs.tar.xz).
//   - PEACOCK_DEBIAN_ROOTFS_SHA256URL: a sidecar sums file for that
//     tarball ("<hash>  <file>"). If unset, checksum verification can
//     only be skipped via PEACOCK_INSECURE_SKIP_VERIFY=1 (fail-closed
//     otherwise).
//   - PEACOCK_INSECURE_SKIP_VERIFY=1: explicit opt-out of checksum
//     verification (only honored on the Debian env path).
const (
	envDebianRootfsURL       = "PEACOCK_DEBIAN_ROOTFS_URL"
	envDebianRootfsSHA256URL = "PEACOCK_DEBIAN_ROOTFS_SHA256URL"
	envInsecureSkipVerify    = "PEACOCK_INSECURE_SKIP_VERIFY"
)

// TarballURL returns the canonical bootstrap tarball URL for a flavor.
// Returns "" for unknown flavors and for debian (which has no stable URL
// and is resolved via env in resolveTarballURL) so callers can produce a
// clear error.
func TarballURL(flavor string) string {
	switch flavor {
	case "arch":
		return ArchBootstrapURL
	case "alpine":
		return AlpineMinirootURL
	case "debian":
		return os.Getenv(envDebianRootfsURL)
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

// EnsureHostChroot idempotently materializes a host chroot for `flavor`
// and returns its path. The end-to-end flow:
//
//  1. fast path: a populated rootfs WITH the toolchain sentinel is
//     reused as-is, no work.
//  2. resolve the download URL (arch + alpine are deterministic stable
//     URLs; debian comes from $PEACOCK_DEBIAN_ROOTFS_URL or errors).
//  3. download the tarball to a temp file, then verify its sha256
//     against the flavor's published sums manifest — FAILING CLOSED if
//     the manifest can't be fetched or lacks our entry.
//  4. extract with the flavor's --strip-components.
//  5. bring up the bind-mounts, install the build toolchain inside,
//     write the sentinel, tear the mounts down.
//
// The (flavor) -> (string, error) signature is load-bearing: the build
// path (runner.SetExecPrefix wiring) depends on it staying stable.
func EnsureHostChroot(flavor string) (string, error) {
	root, err := HostChrootRoot(flavor)
	if err != nil {
		return "", err
	}

	// Fast path: already bootstrapped AND toolchain installed.
	if isProbablyBootstrapped(root) && toolchainReady(root) {
		return root, nil
	}

	// Only download+extract if the rootfs isn't already populated; a
	// half-finished chroot (rootfs present, toolchain not) skips
	// straight to the toolchain install below.
	if !isProbablyBootstrapped(root) {
		url, err := resolveTarballURL(flavor)
		if err != nil {
			return "", err
		}

		if err := os.MkdirAll(root, 0o755); err != nil {
			return "", fmt.Errorf("host: cannot create chroot root %s: %w", root, err)
		}

		tmp, err := os.CreateTemp("", "peacock-host-"+flavor+"-*.tar")
		if err != nil {
			return "", fmt.Errorf("host: cannot create temp file: %w", err)
		}
		tmpPath := tmp.Name()
		_ = tmp.Close()
		defer os.Remove(tmpPath)

		runner.Logf("Downloading %s bootstrap...\n", flavor)
		if err := downloadTarball(url, tmpPath); err != nil {
			return "", err
		}

		filename := url[strings.LastIndex(url, "/")+1:]
		sumsURL := sumsURLFor(flavor, url)
		// Debian env path may explicitly opt out of verification via
		// PEACOCK_INSECURE_SKIP_VERIFY=1 (e.g. a user-supplied rootfs with
		// no published sums). Every other flavor stays fail-closed.
		if sumsURL == "" && flavor == "debian" && os.Getenv(envInsecureSkipVerify) == "1" {
			runner.Logf("WARNING: skipping checksum verification for %s (%s=1)\n", filename, envInsecureSkipVerify)
		} else {
			runner.Logf("Verifying checksum...\n")
			if err := verifyChecksum(tmpPath, sumsURL, filename); err != nil {
				return "", err
			}
		}

		if err := extractTarball(tmpPath, root, flavor); err != nil {
			return "", err
		}
	}

	// Toolchain install needs the bind-mounts up (proc/dev + DNS).
	cleanup, err := mountHostChroot(root)
	if err != nil {
		return "", err
	}
	defer cleanup()

	if err := installToolchain(root, flavor); err != nil {
		return "", err
	}

	return root, nil
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
