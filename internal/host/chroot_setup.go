package host

// chroot_setup.go: the first-time, in-chroot install of the build
// toolchain. After extract the chroot has a base system but no
// compiler; this installs base-devel/build-essential/build-base + git
// per flavor and drops a sentinel so the (slow, network-bound) install
// runs at most once.

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"peacock/internal/runner"
)

// toolchainSentinel is written inside the chroot once the build
// toolchain install succeeds, so subsequent EnsureHostChroot calls can
// skip the (slow, network-bound) install.
const toolchainSentinel = ".peacock-toolchain-ready"

// toolchainReady reports whether the first-time toolchain install has
// already completed for the chroot at root.
func toolchainReady(root string) bool {
	_, err := os.Stat(filepath.Join(root, toolchainSentinel))
	return err == nil
}

// installToolchain performs the first-time, in-chroot install of the
// build toolchain (base-devel/build-essential/build-base + git) for the
// flavor. Idempotent via the toolchainSentinel: a no-op if already
// done. Assumes the caller has the bind-mounts up (it needs proc/dev +
// working DNS); it copies the host resolv.conf in first.
func installToolchain(root, flavor string) error {
	if toolchainReady(root) {
		return nil
	}
	runner.Logf("Installing build toolchain in %s chroot...\n", flavor)

	// Network inside the chroot: copy the host resolv.conf in.
	if data, err := os.ReadFile("/etc/resolv.conf"); err == nil {
		resolv := filepath.Join(root, "etc", "resolv.conf")
		_ = runner.RunCmd(exec.Command("sudo", "mkdir", "-p", filepath.Dir(resolv)))
		// Write via sudo tee so we can land it in a root-owned chroot.
		w := exec.Command("sudo", "tee", resolv)
		w.Stdin = bytes.NewReader(data)
		w.Stdout = runner.LogWriter()
		w.Stderr = runner.LogWriter()
		if err := runner.RunCmd(w); err != nil {
			runner.Logf("Warning: could not write resolv.conf into chroot: %v\n", err)
		}
	}

	var steps [][]string
	switch flavor {
	case "arch":
		// Arch bootstrap ships pacman but an empty keyring.
		steps = [][]string{
			{"chroot", root, "pacman-key", "--init"},
			{"chroot", root, "pacman-key", "--populate", "archlinux"},
			{"chroot", root, "pacman", "-Sy", "--noconfirm", "base-devel", "git"},
		}
	case "debian":
		steps = [][]string{
			{"chroot", root, "apt-get", "update"},
			{"chroot", root, "apt-get", "install", "-y", "build-essential", "git"},
		}
	case "alpine":
		steps = [][]string{
			{"chroot", root, "apk", "add", "--no-cache", "build-base", "git"},
		}
	default:
		return fmt.Errorf("host: no toolchain recipe for flavor %q", flavor)
	}

	for _, step := range steps {
		if err := runner.RunCmd(exec.Command("sudo", step...)); err != nil {
			return fmt.Errorf("host: toolchain step %v failed: %w", step, err)
		}
	}

	// Write the sentinel (inside the chroot, root-owned).
	sentinel := filepath.Join(root, toolchainSentinel)
	if err := runner.RunCmd(exec.Command("sudo", "touch", sentinel)); err != nil {
		return fmt.Errorf("host: writing toolchain sentinel: %w", err)
	}
	runner.Logf("Build toolchain ready in %s chroot\n", flavor)
	return nil
}
