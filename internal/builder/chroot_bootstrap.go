package builder

// Chroot bootstrap: the initial download + extraction + pacman key-init
// + binfmt registration that brings a fresh build chroot up to the point
// where it can host BuildPackageInChroot. Split off chroot_build.go to
// keep this large, self-contained method out of the way of the actual
// package-build pipeline. Helpers (archRootfsURL, qemuStaticName,
// copyFileWithSudo) live in chroot_build.go and are shared.

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"peacock/internal/chroot"
	"peacock/internal/pacman"
	"peacock/internal/runner"
)

// EnsureBuildChroot sets up an Arch-based build root for the given architecture.
func (b *Builder) EnsureBuildChroot(root string, chrootArch string, useQemu bool) error {
	cleanRoot := filepath.Clean(root)
	if cleanRoot == "" || cleanRoot == "." || cleanRoot == string(os.PathSeparator) {
		return fmt.Errorf("refusing to operate on unsafe chroot root: %q", root)
	}
	if !strings.Contains(cleanRoot, string(os.PathSeparator)+"peacock"+string(os.PathSeparator)) {
		return fmt.Errorf("refusing to operate outside peacock workdir: %q", root)
	}

	// If requesting standard x86 build env (Master Chroot)
	if chrootArch == "x86_64" {
		chrootExists := false
		if _, err := os.Stat(filepath.Join(root, "etc", "arch-release")); err == nil {
			chrootExists = true
		}

		// Register binfmt handlers EVERY time (not just on first bootstrap)
		// This ensures QEMU works even if host was rebooted (binfmt is lost on reboot)
		if chrootExists {
			runner.Logln("Master chroot exists, ensuring binfmt handlers are registered...")
			if err := b.registerBinfmtInChroot(root); err != nil {
				runner.Logf("Warning: binfmt registration: %v\n", err)
			}
			return nil
		}

		// Download and extract Arch Linux Bootstrap
		if _, err := os.Stat(root); err == nil {
			rmCmd := exec.Command("sudo", "rm", "-rf", root)
			rmCmd.Stdout = runner.LogWriter()
			rmCmd.Stderr = runner.LogWriter()
			if err := runner.RunCmd(rmCmd); err != nil {
				return fmt.Errorf("failed to clean existing chroot %s: %w", root, err)
			}
		}
		if err := os.MkdirAll(root, 0755); err != nil {
			return err
		}

		rootfsURL, err := archRootfsURL("x86_64")
		if err != nil {
			return err
		}

		tarball, err := b.Download(rootfsURL, "")
		if err != nil {
			return fmt.Errorf("failed to download arch rootfs: %w", err)
		}

		tmpDir, err := os.MkdirTemp("", "peacock-rootfs-")
		if err != nil {
			return err
		}
		defer func() {
			rmTmp := exec.Command("sudo", "rm", "-rf", tmpDir)
			rmTmp.Stdout = runner.LogWriter()
			rmTmp.Stderr = runner.LogWriter()
			_ = runner.RunCmd(rmTmp)
		}()

		cmd := exec.Command("sudo", "tar", "-xf", tarball, "-C", tmpDir) // Extract to tmp
		cmd.Stdout = runner.LogWriter()
		cmd.Stderr = runner.LogWriter()
		if err := runner.RunCmd(cmd); err != nil {
			return fmt.Errorf("failed to extract arch rootfs: %w", err)
		}

		// Locate root inside extraction
		rootSrc := tmpDir
		entries, err := os.ReadDir(tmpDir)
		if err == nil {
			for _, entry := range entries {
				if strings.HasPrefix(entry.Name(), "root.") {
					rootSrc = filepath.Join(tmpDir, entry.Name())
					break
				}
			}
		}
		// Also check strict "root.x86_64" usually found in bootstrap
		preferred := filepath.Join(tmpDir, "root.x86_64")
		if info, err := os.Stat(preferred); err == nil && info.IsDir() {
			rootSrc = preferred
		}

		// Move to final location
		rmRoot := exec.Command("sudo", "rm", "-rf", root)
		rmRoot.Stdout = runner.LogWriter()
		rmRoot.Stderr = runner.LogWriter()
		_ = runner.RunCmd(rmRoot)

		mkRoot := exec.Command("sudo", "mkdir", "-p", root)
		mkRoot.Stdout = runner.LogWriter()
		mkRoot.Stderr = runner.LogWriter()
		if err := runner.RunCmd(mkRoot); err != nil {
			return err
		}

		// Copy content
		copyCmd := exec.Command("sudo", "cp", "-a", rootSrc+string(os.PathSeparator)+".", root)
		copyCmd.Stdout = runner.LogWriter()
		copyCmd.Stderr = runner.LogWriter()
		if err := runner.RunCmd(copyCmd); err != nil {
			return fmt.Errorf("failed to install arch rootfs: %w", err)
		}

		// Bootstrap pacman in master using internal pacman (avoid host dependency)
		// We need to mount proc/sys/dev first
		if err := chroot.MountWithSudo(root); err != nil {
			return err
		}
		defer chroot.UnmountWithSudo(root)

		// Enable mirrors in master chroot and disable space check
		mirrorlistPath := filepath.Join(root, "etc", "pacman.d", "mirrorlist")
		if err := runner.RunCmd(exec.Command("sudo", "sed", "-i", "s/^#Server/Server/", mirrorlistPath)); err != nil {
			runner.Logf("Warning: failed to update mirrorlist: %v\n", err)
		}

		confPath := filepath.Join(root, "etc", "pacman.conf")
		if err := runner.RunCmd(exec.Command("sudo", "sed", "-i", "s/^CheckSpace/#CheckSpace/", confPath)); err != nil {
			runner.Logf("Warning: failed to disable CheckSpace: %v\n", err)
		}

		// Create /etc/mtab symlink in Master so pacman can check free space/mounts properly
		if err := runner.RunCmd(exec.Command("sudo", "ln", "-sf", "/proc/self/mounts", filepath.Join(root, "etc", "mtab"))); err != nil {
			runner.Logf("Warning: failed to link /etc/mtab: %v\n", err)
		}

		// Initialize keys inside
		if err := runner.RunCmd(exec.Command("sudo", "chroot", root, "pacman-key", "--init")); err != nil {
			// Continue? Keys might fail but let's try populate
		}
		if err := runner.RunCmd(exec.Command("sudo", "chroot", root, "pacman-key", "--populate", "archlinux")); err != nil {
			// Ignore population error if keys aren't perfect, we can use --noconfirm
		}

		// Install packages inside
		// We need network in chroot (resolv.conf)
		if _, err := os.Stat("/etc/resolv.conf"); err == nil {
			_ = copyFileWithSudo("/etc/resolv.conf", filepath.Join(root, "etc", "resolv.conf"))
		}

		pkgs := []string{"base-devel", "qemu-user-static", "qemu-user-static-binfmt", "git"}
		args := append([]string{"chroot", root, "pacman", "-Sy", "--noconfirm"}, pkgs...)
		if err := runner.RunCmd(exec.Command("sudo", args...)); err != nil {
			return fmt.Errorf("failed to install packages in master chroot: %w", err)
		}

		// Register the qemu-user-static binfmt handlers from the chroot's
		// /usr/lib/binfmt.d (shared impl — see registerBinfmtInChroot).
		if err := b.registerBinfmtInChroot(root); err != nil {
			runner.Logf("Warning: binfmt registration: %v\n", err)
		}

		return nil
	}

	// Case: ARM / Target Chroot
	// We require the master x86 chroot to function.
	masterRoot := filepath.Join(filepath.Dir(root), "..", "build-chroot", "master-x86_64") // naming convention?
	// Let's fix pathing: root is usually .../build-chroot/armv7
	// master is .../build-chroot/x86_64
	masterRoot = filepath.Join(filepath.Dir(root), "x86_64")

	if err := b.EnsureBuildChroot(masterRoot, "x86_64", false); err != nil {
		return fmt.Errorf("failed to ensure master chroot: %w", err)
	}

	// Mount Master chroot pseudo-filesystems so pacman (and mtab) works inside
	if err := chroot.MountWithSudo(masterRoot); err != nil {
		return fmt.Errorf("failed to mount master chroot: %w", err)
	}
	defer chroot.UnmountWithSudo(masterRoot)

	if _, err := os.Stat(filepath.Join(root, "etc", "arch-release")); err == nil {
		// Ensure QEMU is present even if chroot exists (needed for execution)
		qemuName := qemuStaticName(chrootArch)
		if qemuName != "" {
			qemuSrc := filepath.Join(masterRoot, "usr", "bin", qemuName)
			qemuDst := filepath.Join(root, "usr", "bin", qemuName)
			// Ensure destination usr/bin exists
			if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", filepath.Dir(qemuDst))); err != nil {
				return fmt.Errorf("failed to ensure dest dir for qemu: %w", err)
			}
			if err := copyFileWithSudo(qemuSrc, qemuDst); err != nil {
				return fmt.Errorf("failed to copy %s to target: %w", qemuName, err)
			}
		}
		return nil // Already exists
	}

	// We create the ARM chroot using tools from masterRoot
	if err := os.MkdirAll(root, 0755); err != nil {
		return err
	}
	// Create var/lib/pacman so pacman -r works
	if err := os.MkdirAll(filepath.Join(root, "var", "lib", "pacman"), 0755); err != nil {
		return err
	}

	// Keep a per-chroot pacman cache. Do not bind the global peacock cache here:
	// mixing packages from different repos/arches causes checksum mismatches.
	targetCache := filepath.Join(root, "var", "cache", "pacman", "pkg")
	if err := os.MkdirAll(targetCache, 0755); err != nil {
		return err
	}

	// Symlink /proc/self/mounts to /etc/mtab for pacman disk check
	// We need /etc to exist first (it does, likely created by MkdirAll above or future step? No, MkdirAll only created root and var/lib/pacman)
	// Actually, target root is currently empty except for var/lib/pacman.
	if err := os.MkdirAll(filepath.Join(root, "etc"), 0755); err != nil {
		return err
	}
	_ = runner.RunCmd(exec.Command("sudo", "ln", "-sf", "/proc/self/mounts", filepath.Join(root, "etc", "mtab")))

	// 1. Generate pacman config for target arch
	targetPacmanArch := chrootArch
	if targetPacmanArch == "armv7" {
		targetPacmanArch = "armv7h"
	}
	if err := pacman.GenerateConfig(root, targetPacmanArch); err != nil {
		return err
	}
	// Disable CheckSpace in target config to avoid mount point errors in nested chroot
	targetConfPath := filepath.Join(root, "etc", "pacman.conf")
	if err := runner.RunCmd(exec.Command("sudo", "sed", "-i", "s/^CheckSpace/#CheckSpace/", targetConfPath)); err != nil {
		runner.Logf("Warning: failed to disable CheckSpace in target: %v\n", err)
	}

	// 2. Install 'base' and 'qemu-user-static' into root using Master's pacman
	// We need to execute `pacman -r /target ...` inside master.
	// We assume master has networking and keys setup.

	// Issue: Master chroot paths.
	// We probably need to bind mount `root` (target) into `masterRoot/mnt` to access it easily.
	// Or just use the full path if master is unprivileged? No, `arch-chroot` or `chroot` needs relative.
	// We will rely on `pacman.Install` with `execRoot`.
	// BUT `pacman.Install` runs `chroot execRoot pacman -r target`.
	// `target` must be visible inside `execRoot`.

	// Bind mount target into master
	mountPoint := filepath.Join(masterRoot, "mnt", "target")
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", mountPoint)); err != nil {
		return err
	}
	// Bind mount
	bindCmd := exec.Command("sudo", "mount", "--rbind", root, mountPoint)
	if err := runner.RunCmd(bindCmd); err != nil {
		return fmt.Errorf("failed to bind mount target into master: %w", err)
	}
	defer func() {
		_ = runner.RunCmd(exec.Command("sudo", "umount", mountPoint))
	}()

	// Now install. Target path inside master is "/mnt/target".
	// Config file is at "/mnt/target/etc/pacman.conf"

	// Now install. Target path inside master is "/mnt/target".
	// Config file is at "/mnt/target/etc/pacman.conf"

	// We do NOT install qemu-user-static inside the ARM target.
	// It is an x86 package.
	// We rely on the Master chroot (or Host) having binfmt_misc set up with the 'F' flag (Fix Binary),
	// which allows the kernel to use the interpreter (qemu-arm-static) from the master/host
	// without needing it present inside the target chroot.

	// Install the provider explicitly to avoid interactive provider prompts
	// (libxtables.so=12-64: iptables vs iptables-nft) during chroot bootstrap.
	pkgs := []string{"iptables", "base"}

	if err := pacman.Install("/mnt/target", "/mnt/target/etc/pacman.conf", pkgs, "/mnt/target/var/cache/pacman/pkg", false, masterRoot); err != nil {
		return fmt.Errorf("failed to bootstrap ARM chroot from master: %w", err)
	}

	// 3. Copy qemu-arm-static from Master to Target
	// Required for execution on host systems where binfmt_misc expects interpreter inside chroot
	// or if the 'F' flag was not used during registration.
	qemuName := qemuStaticName(chrootArch)
	if qemuName != "" {
		qemuSrc := filepath.Join(masterRoot, "usr", "bin", qemuName)
		qemuDst := filepath.Join(root, "usr", "bin", qemuName)
		// Ensure destination usr/bin exists (it should from base install)
		if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", filepath.Dir(qemuDst))); err != nil {
			return fmt.Errorf("failed to ensure dest dir for qemu: %w", err)
		}
		if err := copyFileWithSudo(qemuSrc, qemuDst); err != nil {
			return fmt.Errorf("failed to copy %s to target: %w", qemuName, err)
		}
	}

	return nil
}
