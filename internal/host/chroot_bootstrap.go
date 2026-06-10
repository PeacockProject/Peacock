package host

// chroot_bootstrap.go: the download + checksum-verify + extract half of
// the host-chroot bootstrap. This is the pmbootstrap-style idea applied
// one level up from internal/builder's per-arch BUILD chroots: the host
// needs only chroot + tar + curl, and everything else (pacman/apt/apk,
// the build toolchain) lives inside a managed chroot we materialize here.
//
// The flow, per flavor:
//   1. resolve the tarball URL (arch + alpine are deterministic stable
//      URLs; debian comes from $PEACOCK_DEBIAN_ROOTFS_URL or a clear error
//      — Debian has no stable rootfs tarball).
//   2. curl the tarball to a temp file.
//   3. download the sums file (arch: fixed geo-mirror sha256sums.txt;
//      alpine: per-file .sha256 sidecar; debian:
//      $PEACOCK_DEBIAN_ROOTFS_SHA256URL), look up the expected sha256 for
//      our filename, compute the local sha256, compare. FAIL CLOSED — if
//      the sums file can't be fetched or the entry is missing we error
//      out rather than silently trusting an unverified tarball (the
//      debian env path may opt out via PEACOCK_INSECURE_SKIP_VERIFY=1).
//   4. tar -xpf with the right --strip-components for the flavor's layout.

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"

	"peacock/internal/runner"
)

// archSumsURL is Arch's published sha256sums manifest alongside the
// stable bootstrap tarball on the geo mirror. It lists both the stable
// and the dated filenames (same hash); expectedHashFor matches by
// basename.
const archSumsURL = "https://geo.mirror.pkgbuild.com/iso/latest/sha256sums.txt"

// archBootstrapFilePattern matches the dated bootstrap tarball filename
// inside a latest/ directory listing, e.g.
// archlinux-bootstrap-2024.06.01-x86_64.tar.zst. The happy path uses the
// stable .tar.zst URL directly (see ArchBootstrapURL); this pattern +
// parseArchBootstrapListing are retained as a pure, tested fallback for
// any future listing-scrape use.
var archBootstrapFilePattern = regexp.MustCompile(`archlinux-bootstrap-[0-9.]+-x86_64\.tar\.(?:zst|gz)`)

// stripComponentsFor returns the tar --strip-components value for a
// flavor's bootstrap tarball layout:
//   - arch: the bootstrap tarball nests everything under a single
//     root.x86_64/ top-level dir, so strip 1.
//   - debian: a user-supplied rootfs tarball is expected to extract flat.
//   - alpine: the miniroot tarball extracts flat too.
func stripComponentsFor(flavor string) int {
	switch flavor {
	case "arch":
		return 1
	default:
		return 0
	}
}

// sumsURLFor returns the checksum-manifest URL for a flavor.
//   - arch: a fixed sha256sums.txt on the geo mirror.
//   - alpine: a per-file sidecar, "<tarballURL>.sha256".
//   - debian: $PEACOCK_DEBIAN_ROOTFS_SHA256URL, or "" (which forces
//     fail-closed in verifyChecksum unless the caller opted into the
//     insecure-skip env).
//
// Returns "" for unknown flavors so callers fail closed.
func sumsURLFor(flavor, tarballURL string) string {
	switch flavor {
	case "arch":
		return archSumsURL
	case "alpine":
		return tarballURL + ".sha256"
	case "debian":
		return os.Getenv(envDebianRootfsSHA256URL)
	default:
		return ""
	}
}

// resolveTarballURL returns the concrete download URL for a flavor.
//   - arch: the stable .tar.zst URL directly (geo mirror resolves it to
//     the newest dated build — no listing scrape needed).
//   - alpine: deterministic.
//   - debian: $PEACOCK_DEBIAN_ROOTFS_URL, or a clear, actionable error.
func resolveTarballURL(flavor string) (string, error) {
	if flavor == "debian" {
		if url := TarballURL("debian"); url != "" {
			return url, nil
		}
		return "", fmt.Errorf(
			"host: host-chroot bootstrap for the debian flavor isn't wired up to a built-in URL — "+
				"Debian publishes no stable rootfs tarball (the cloud genericcloud image is a disk image, not a chroot). "+
				"Either use `--use-host-chroot arch` or `--use-host-chroot alpine`, "+
				"or set $%s to a flat Debian rootfs tarball (e.g. a debuerreotype / docker-debian-artifacts rootfs.tar.xz) "+
				"and $%s to its sums sidecar (or set $%s=1 to skip verification)",
			envDebianRootfsURL, envDebianRootfsSHA256URL, envInsecureSkipVerify)
	}
	url := TarballURL(flavor)
	if url == "" {
		return "", fmt.Errorf("host: no bootstrap tarball URL known for flavor %q", flavor)
	}
	return url, nil
}

// parseArchBootstrapListing extracts the dated bootstrap tarball
// filename from an archive.archlinux.org latest/ directory listing.
// Pure (no network): given the HTML body, returns the bare filename
// (e.g. archlinux-bootstrap-2024.06.01-x86_64.tar.gz) or an error if no
// match is present.
func parseArchBootstrapListing(html string) (string, error) {
	matches := archBootstrapFilePattern.FindAllString(html, -1)
	if len(matches) == 0 {
		return "", fmt.Errorf("host: no archlinux-bootstrap tarball found in listing")
	}
	// The listing may reference the same name multiple times (anchor
	// href + visible text). Pick the lexically-greatest unique match so
	// that, if several dates are present, we take the newest — dates are
	// zero-padded YYYY.MM.DD so lexical == chronological order.
	newest := matches[0]
	for _, m := range matches[1:] {
		if m > newest {
			newest = m
		}
	}
	return newest, nil
}

// downloadTarball curls a URL to dest with retries, streaming progress
// through the runner log writer.
func downloadTarball(url, dest string) error {
	runner.Logf("Downloading %s -> %s\n", url, dest)
	cmd := exec.Command("curl", "-fSL", "--retry", "3", "-o", dest, url)
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("host: download failed for %s: %w", url, err)
	}
	return nil
}

// verifyChecksum downloads the sums manifest at sumsURL, looks up the
// expected sha256 for filename, computes the local sha256 of
// tarballPath, and compares. FAILS CLOSED: any failure to fetch or
// parse the sums file, or a missing entry, is an error — we never skip
// verification silently, because this is a security boundary.
func verifyChecksum(tarballPath, sumsURL, filename string) error {
	if sumsURL == "" {
		return fmt.Errorf("host: no checksum URL for %s (refusing to skip verification)", filename)
	}
	runner.Logf("Verifying checksum via %s\n", sumsURL)
	sums, err := runner.RunOutput(exec.Command("curl", "-fsSL", sumsURL))
	if err != nil {
		return fmt.Errorf("host: failed to fetch checksum manifest %s (failing closed): %w", sumsURL, err)
	}
	expected, err := expectedHashFor(sums, filename)
	if err != nil {
		return err
	}
	actual, err := sha256File(tarballPath)
	if err != nil {
		return fmt.Errorf("host: failed to hash %s: %w", tarballPath, err)
	}
	if !strings.EqualFold(actual, expected) {
		return fmt.Errorf("host: checksum mismatch for %s: expected %s got %s", filename, expected, actual)
	}
	runner.Logf("Checksum OK for %s\n", filename)
	return nil
}

// expectedHashFor extracts the sha256 for filename from the content of
// a sums manifest. Pure (no network): handles both the Arch/Alpine
// "<hash>  <file>" form and the Debian SHA256SUMS form (same shape,
// sometimes with a leading "*" or "./" on the path). Returns an error
// if no matching entry is present.
func expectedHashFor(sumsContent, filename string) (string, error) {
	base := filepath.Base(filename)
	for _, line := range strings.Split(sumsContent, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		hash := fields[0]
		// The filename field may carry a binary-mode "*" marker or a
		// "./" prefix; normalize before comparing by basename.
		name := fields[len(fields)-1]
		name = strings.TrimPrefix(name, "*")
		name = strings.TrimPrefix(name, "./")
		if filepath.Base(name) == base {
			return hash, nil
		}
	}
	return "", fmt.Errorf("host: no checksum entry for %q in manifest (failing closed)", filename)
}

// sha256File streams a file through crypto/sha256 and returns the hex
// digest. Streaming (not ReadFile) keeps memory flat for ~600MB rootfs
// tarballs.
func sha256File(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

// extractTarball untars tarballPath into destRoot with the per-flavor
// --strip-components applied. Uses sudo because the rootfs entries carry
// privileged ownership/permissions (-p preserves them); destRoot lives
// under the peacock workdir.
func extractTarball(tarballPath, destRoot, flavor string) error {
	if err := os.MkdirAll(destRoot, 0o755); err != nil {
		return fmt.Errorf("host: cannot create chroot root %s: %w", destRoot, err)
	}
	args := []string{"tar", "-xpf", tarballPath, "-C", destRoot}
	if n := stripComponentsFor(flavor); n > 0 {
		args = append(args, fmt.Sprintf("--strip-components=%d", n))
	}
	runner.Logf("Extracting %s into %s (strip=%d)\n", filepath.Base(tarballPath), destRoot, stripComponentsFor(flavor))
	cmd := exec.Command("sudo", args...)
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("host: extract failed for %s: %w", tarballPath, err)
	}
	return nil
}
