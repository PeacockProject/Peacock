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

	// Each step is a shell command run inside the chroot. We route
	// through `/bin/sh -c` with an explicit PATH because `sudo chroot
	// <root> <cmd>` does a PATH lookup using sudo's sanitized PATH
	// (typically /usr/bin:/bin), which misses package managers that
	// live in /sbin — Alpine's apk is at /sbin/apk, for instance. A
	// fixed PATH covering the usual bin/sbin dirs resolves the binary
	// uniformly across flavors. apt-get also wants DEBIAN_FRONTEND set
	// so it never blocks on an interactive prompt.
	const chrootPath = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

	var cmds []string
	switch flavor {
	case "arch":
		// Arch bootstrap ships pacman with an empty keyring AND an empty
		// mirrorlist (every Server line commented out), so `pacman -Sy`
		// fails with "no servers configured" until we write one. The geo
		// mirror resolves to a nearby CDN node. Single quotes keep the
		// shell from expanding pacman's $repo/$arch placeholders.
		cmds = []string{
			"printf 'Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch\\n' > /etc/pacman.d/mirrorlist",
			// pacman's CheckSpace statvfs's the install root, but inside a
			// chroot the mount table doesn't reflect the real backing fs,
			// so it spuriously reports "not enough free disk space" even
			// with hundreds of GB free. arch-install-scripts / pmbootstrap
			// disable it the same way when installing into a chroot.
			"sed -i 's/^CheckSpace/#CheckSpace/' /etc/pacman.conf",
			"pacman-key --init",
			"pacman-key --populate archlinux",
			"pacman -Sy --noconfirm base-devel git",
		}
	case "debian":
		cmds = []string{
			"export DEBIAN_FRONTEND=noninteractive; apt-get update",
			"export DEBIAN_FRONTEND=noninteractive; apt-get install -y build-essential git",
		}
	case "alpine":
		cmds = []string{
			"apk add --no-cache build-base git",
		}
	default:
		return fmt.Errorf("host: no toolchain recipe for flavor %q", flavor)
	}

	for _, c := range cmds {
		shc := chrootPath + " " + c
		cmd := exec.Command("sudo", "chroot", root, "/bin/sh", "-c", shc)
		if err := runner.RunCmd(cmd); err != nil {
			return fmt.Errorf("host: toolchain step [%s] failed: %w", c, err)
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
