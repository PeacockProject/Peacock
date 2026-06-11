package builder

// Chroot dep installation: stages a package manifest's build_deps into
// the chroot via pacman, sanitizing the chroot's pacman.conf and binding
// the host build cache. Split off chroot_build.go to keep the build-deps
// machinery separate from the package-build entry point. Shared helpers
// (copyFileWithSudo, etc.) still live in chroot_build.go.

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"peacock/internal/pacman"
	"peacock/internal/runner"
)

func (b *Builder) installBuildDeps(root string, deps []string, execRoot, flavor, arch string) error {
	if len(deps) == 0 {
		return nil
	}
	// Persistent per-(flavor, arch) distro download cache, bind-mounted as the
	// chroot's pacman cachedir so build_deps aren't re-fetched on a fresh chroot.
	distroCache := b.DistroPkgCacheDir(flavor, arch)

	targetPath := root
	confPath := filepath.Join(root, "etc", "pacman.conf")

	if execRoot != "" {
		// We are using a master chroot.
		// We need to mount root into execRoot/mnt/target first?
		// Or assume caller has done mounting?
		// To be safe and robust, we should handle mount here or assume a standard mount point.
		// Let's assume standard mount point "/mnt/target" is used if execRoot is set,
		// and that 'root' passed here corresponds to that.
		// Actually, pacman.Install handles the chroot command.
		// But valid paths inside execRoot need to be passed.

		// Strategy: Bind mount 'root' to 'execRoot/mnt/deps-target' just for this op.
		mountPoint := filepath.Join(execRoot, "mnt", "deps-target")
		if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", mountPoint)); err != nil {
			return err
		}
		if err := runner.RunCmd(exec.Command("sudo", "mount", "--rbind", root, mountPoint)); err != nil {
			return err
		}
		defer runner.RunCmd(exec.Command("sudo", "umount", mountPoint))

		// Mount cache
		cacheMount := filepath.Join(mountPoint, "var", "cache", "pacman", "pkg")
		if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", cacheMount)); err != nil {
			return err
		}
		if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", distroCache, cacheMount)); err != nil {
			return err
		}
		defer runner.RunCmd(exec.Command("sudo", "umount", cacheMount))

		targetPath = "/mnt/deps-target"
		confPath = filepath.Join(targetPath, "etc", "pacman.conf")

		// If we are installing deps, we might need a sanitized config inside the execRoot?
		// pacman.SanitizeConfig creates a temp file on HOST.
		// If we pass that temp file path to `chroot execRoot pacman ...`, the chroot won't see it
		// because /tmp is usually not shared unless we bind mount it.
		// This is a complication.

		// Alternative: Generate a config inside the mounted target?
		// Or just copy the sanitized config into the target temporarily.

		tmpConf, cleanup, err := pacman.SanitizeConfig(filepath.Join(root, "etc", "pacman.conf"))
		if err != nil {
			return err
		}
		defer cleanup()

		// Copy tmpConf to root/tmp/pacman-deps.conf
		innerConfPath := filepath.Join(root, "tmp", "pacman-deps.conf")
		if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", filepath.Dir(innerConfPath))); err != nil {
			return err
		}
		if err := copyFileWithSudo(tmpConf, innerConfPath); err != nil {
			return err
		}

		// Inside chroot, path is /mnt/deps-target/tmp/pacman-deps.conf
		// Pass explicit cachedir
		return pacman.Install(targetPath, filepath.Join(targetPath, "tmp", "pacman-deps.conf"), deps, filepath.Join(targetPath, "var/cache/pacman/pkg"), false, execRoot)
	}

	confPath = filepath.Join(root, "etc", "pacman.conf")
	startConf, cleanup, err := pacman.SanitizeConfig(confPath)
	if err != nil {
		return fmt.Errorf("failed to sanitize pacman config: %w", err)
	}
	defer cleanup()

	cacheMount := filepath.Join(root, "var", "cache", "pacman", "pkg")
	if err := os.MkdirAll(cacheMount, 0755); err != nil {
		return err
	}
	// Bind the persistent per-arch distro cache over the chroot's pacman cache
	// so downloads survive the chroot and a fresh build reuses them.
	if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", distroCache, cacheMount)); err != nil {
		return err
	}
	defer runner.RunCmd(exec.Command("sudo", "umount", cacheMount))

	// Using pacman to install build deps from the bind-mounted persistent cache.
	return pacman.Install(root, startConf, deps, cacheMount, false, "")
}
