// Package ports locates the peacock-ports tree and, when absent, clones
// it so a standalone `peacock` binary can build without the maintainer's
// dev-layout symlink.
//
// Resolution order (Resolve):
//  1. $PEACOCK_PORTS_DIR                       (explicit override)
//  2. ./peacock-ports                          (dev-layout symlink, cwd-relative)
//  3. <varDir>/peacock-ports                   (the auto-clone target)
//  4. exe-relative ../peacock-ports and
//     ../../../../peacock-ports                (legacy sibling fallbacks)
//
// where varDir is ~/.local/var/peacock — the same dir the rest of Peacock
// uses for its workdir cache and host chroots. Every candidate must contain
// a device/ subdir to be accepted; an empty directory is not a valid tree.
//
// Ensure clones the tree into <varDir>/peacock-ports when Resolve finds
// nothing. The clone URL comes from $PEACOCK_PORTS_URL, defaulting to the
// public HTTPS form. The repo may be private; a maintainer with SSH access
// can point at the SSH remote with:
//
//	PEACOCK_PORTS_URL=git@github.com:PeacockProject/peacock-ports.git
package ports

import (
	"fmt"
	"os"
	"path/filepath"

	"peacock/internal/runner"
)

// DefaultURL is the public clone URL used when $PEACOCK_PORTS_URL is unset.
const DefaultURL = "https://github.com/PeacockProject/peacock-ports"

// varDir returns ~/.local/var/peacock — the shared Peacock var directory
// (parent of the workdir cache and the host-chroots dir). It is also the
// auto-clone target's parent.
func varDir() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("ports: cannot resolve $HOME: %w", err)
	}
	return filepath.Join(home, ".local", "var", "peacock"), nil
}

// hasDevice reports whether dir looks like a real ports checkout (it
// contains a device/ subdir). This is the sanity guard that keeps an
// empty or half-cloned directory from being accepted.
func hasDevice(dir string) bool {
	if dir == "" {
		return false
	}
	info, err := os.Stat(filepath.Join(dir, "device"))
	return err == nil && info.IsDir()
}

// Resolve finds an existing peacock-ports checkout. It returns the first
// candidate that contains a device/ subdir and never clones. found is
// false when no candidate qualifies.
func Resolve() (root string, found bool) {
	if v := os.Getenv("PEACOCK_PORTS_DIR"); v != "" && hasDevice(v) {
		return v, true
	}
	if hasDevice("peacock-ports") {
		return "peacock-ports", true
	}
	if vd, err := varDir(); err == nil {
		if c := filepath.Join(vd, "peacock-ports"); hasDevice(c) {
			return c, true
		}
	}
	if exe, err := os.Executable(); err == nil {
		for _, c := range []string{
			filepath.Join(filepath.Dir(exe), "..", "peacock-ports"),
			filepath.Join(filepath.Dir(exe), "..", "..", "..", "..", "peacock-ports"),
		} {
			if hasDevice(c) {
				return c, true
			}
		}
	}
	return "", false
}

// cloneURL returns the clone URL, honoring $PEACOCK_PORTS_URL and falling
// back to DefaultURL. Pulled out so tests can assert the env-override
// selection without invoking git.
func cloneURL() string {
	if u := os.Getenv("PEACOCK_PORTS_URL"); u != "" {
		return u
	}
	return DefaultURL
}

// cloneArgs builds the git argument vector for a shallow clone. Pure and
// side-effect-free so a test can assert it without running git.
func cloneArgs(url, dest string) []string {
	return []string{"clone", "--depth", "1", url, dest}
}

// Ensure returns an existing peacock-ports checkout, cloning one into
// <varDir>/peacock-ports if none is found. The clone is shallow (depth 1)
// and uses cloneURL(). After cloning, the tree is re-verified to contain
// a device/ subdir; a clone that lands without one is reported as an error
// rather than silently returned.
func Ensure() (root string, err error) {
	if r, ok := Resolve(); ok {
		return r, nil
	}

	vd, err := varDir()
	if err != nil {
		return "", err
	}
	if err := os.MkdirAll(vd, 0755); err != nil {
		return "", fmt.Errorf("ports: creating %s: %w", vd, err)
	}
	dest := filepath.Join(vd, "peacock-ports")
	url := cloneURL()

	runner.Logf("Fetching peacock-ports…\n")
	if err := runner.Run("git", cloneArgs(url, dest)...); err != nil {
		return "", fmt.Errorf("ports: cloning %s into %s: %w", url, dest, err)
	}

	if !hasDevice(dest) {
		return "", fmt.Errorf("ports: cloned %s but %s has no device/ subdir", url, dest)
	}
	return dest, nil
}
