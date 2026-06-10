package host

// chroot_mount.go: bind-mount management for the host chroot. Reuses
// internal/chroot's sudo mount helpers (dev/proc/sys/devpts) and adds
// three host-chroot-specific binds:
//   - peacock-ports tree (read-only, so port builds inside can read
//     manifests),
//   - the workdir cache (so cached artifacts persist across the
//     host/chroot boundary), and
//   - /dev/bus/usb (so fastboot inside the chroot can reach devices on
//     the flash path).

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"peacock/internal/chroot"
	"peacock/internal/ports"
	"peacock/internal/runner"
)

// portsDir resolves the peacock-ports tree via ports.Resolve (the shared
// resolution order). Returns "" if none is found (the ports bind is then
// skipped — it is an optimization, not a hard requirement for the
// toolchain install). This never clones: bind-mounting is a mount-time
// optimization, and a fetch belongs to the build path, not the mount
// helper. The resolved path is made absolute so the bind source is valid
// regardless of the chroot's cwd.
func portsDir() string {
	root, ok := ports.Resolve()
	if !ok {
		return ""
	}
	if abs, err := filepath.Abs(root); err == nil {
		return abs
	}
	return root
}

// workdirCache returns the peacock workdir cache path
// (~/.local/var/peacock). This is also the parent of the host-chroots
// dir, so it always exists by the time we bind it.
func workdirCache() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("host: cannot resolve $HOME: %w", err)
	}
	return filepath.Join(home, ".local", "var", "peacock"), nil
}

// bindMountRO bind-mounts src at dst read-only, creating dst first.
// Skips if dst is already a mountpoint (idempotent across retries).
func bindMountRO(src, dst string) error {
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", dst)); err != nil {
		return fmt.Errorf("host: mkdir %s: %w", dst, err)
	}
	if err := exec.Command("mountpoint", "-q", dst).Run(); err == nil {
		return nil // already mounted
	}
	if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", "-o", "ro", src, dst)); err != nil {
		return fmt.Errorf("host: bind-mount %s -> %s: %w", src, dst, err)
	}
	return nil
}

// bindMountRW bind-mounts src at dst read-write, creating dst first.
// Skips if dst is already a mountpoint.
func bindMountRW(src, dst string) error {
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", dst)); err != nil {
		return fmt.Errorf("host: mkdir %s: %w", dst, err)
	}
	if err := exec.Command("mountpoint", "-q", dst).Run(); err == nil {
		return nil
	}
	if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", src, dst)); err != nil {
		return fmt.Errorf("host: bind-mount %s -> %s: %w", src, dst, err)
	}
	return nil
}

// mountHostChroot brings up every bind-mount the chroot needs and
// returns a cleanup func that tears them down in reverse order. The
// cleanup tolerates partial mounts (UnmountPathWithSudo is a no-op on
// paths that aren't mountpoints), so it is safe to defer immediately
// after a partial-failure return.
//
// Mount set (and teardown reverse order):
//  1. dev / dev/pts / proc / sys     — via chroot.MountWithSudo
//  2. <root>/peacock-ports           — peacock-ports tree, read-only
//  3. <root><cache>                  — workdir cache, same path RW
//  4. <root>/dev/bus/usb             — for the fastboot/flash path
func mountHostChroot(root string) (func(), error) {
	var extra []string // dst paths to unmount, in mount order
	cleanup := func() {
		// Reverse order: extras first (newest last), then the pseudo-fs.
		for i := len(extra) - 1; i >= 0; i-- {
			_ = chroot.UnmountPathWithSudo(extra[i])
		}
		_ = chroot.UnmountWithSudo(root)
	}

	// 1. dev/pts/proc/sys via the shared helper.
	if err := chroot.MountWithSudo(root); err != nil {
		cleanup()
		return nil, fmt.Errorf("host: mounting pseudo-filesystems: %w", err)
	}

	// 2. peacock-ports read-only (best-effort: skip if not resolvable).
	if pd := portsDir(); pd != "" {
		dst := filepath.Join(root, "peacock-ports")
		if err := bindMountRO(pd, dst); err != nil {
			cleanup()
			return nil, err
		}
		extra = append(extra, dst)
	}

	// 3. workdir cache at the SAME path inside the chroot so cached
	//    artifact paths resolve identically on both sides.
	if cache, err := workdirCache(); err == nil {
		if _, statErr := os.Stat(cache); statErr == nil {
			dst := filepath.Join(root, cache)
			if err := bindMountRW(cache, dst); err != nil {
				cleanup()
				return nil, err
			}
			extra = append(extra, dst)
		}
	}

	// 4. /dev/bus/usb for the flash path (fastboot inside the chroot
	//    needs to see USB devices). Best-effort: absent on hosts with no
	//    USB bus exported.
	if _, err := os.Stat("/dev/bus/usb"); err == nil {
		dst := filepath.Join(root, "dev", "bus", "usb")
		if err := bindMountRW("/dev/bus/usb", dst); err != nil {
			cleanup()
			return nil, err
		}
		extra = append(extra, dst)
	}

	return cleanup, nil
}
