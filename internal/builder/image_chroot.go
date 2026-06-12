package builder

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"peacock/internal/chroot"
	"peacock/internal/pacman"
	"peacock/internal/runner"
)

// EnsureImageBuildChroot sets up the image-build-chroot environment.
// This is a separate x86_64 chroot used exclusively for assembling disk images.
// It contains QEMU for installing ARM packages and image tools (parted, e2fsprogs, etc.)
func (b *Builder) EnsureImageBuildChroot() (string, error) {
	root := filepath.Join(filepath.Dir(b.CacheDir), "image-build-chroot", "x86_64")

	// Check if already exists
	if _, err := os.Stat(filepath.Join(root, "etc", "arch-release")); err == nil {
		runner.Logln("Image build chroot already exists, skipping bootstrap")
		// Ensure special filesystems are mounted
		if err := chroot.MountWithSudo(root); err != nil {
			runner.Logf("Warning: failed to mount chroot filesystems: %v\n", err)
		}
		// Ensure required tooling is present even after partial/failed previous runs.
		if err := b.installImageTools(root); err != nil {
			chroot.UnmountWithSudo(root)
			return "", fmt.Errorf("failed to install image tools: %w", err)
		}
		return root, nil
	}

	runner.Logln("Setting up image build chroot...")

	// Create directory
	if err := os.MkdirAll(root, 0755); err != nil {
		return "", fmt.Errorf("failed to create image chroot dir: %w", err)
	}

	// Bootstrap base system
	if err := b.bootstrapImageChroot(root); err != nil {
		return "", fmt.Errorf("failed to bootstrap image chroot: %w", err)
	}

	// Mount special filesystems
	if err := chroot.MountWithSudo(root); err != nil {
		return "", fmt.Errorf("failed to mount special filesystems: %w", err)
	}

	// Install image-building tools
	if err := b.installImageTools(root); err != nil {
		chroot.UnmountWithSudo(root)
		return "", fmt.Errorf("failed to install image tools: %w", err)
	}

	runner.Logln("Image build chroot ready")
	return root, nil
}

// bootstrapImageChroot downloads and extracts the Arch Linux bootstrap tarball
func (b *Builder) bootstrapImageChroot(root string) error {
	runner.Logln("Bootstrapping image build chroot...")

	// Use cached bootstrap if available
	bootstrapTarball := filepath.Join(b.CacheDir, "archlinux-bootstrap-x86_64.tar.zst")
	if _, err := os.Stat(bootstrapTarball); os.IsNotExist(err) {
		return fmt.Errorf("bootstrap tarball not found in cache: %s (should have been downloaded during build chroot setup)", bootstrapTarball)
	}

	// Extract bootstrap
	extractCmd := exec.Command("sudo", "tar", "-xf", bootstrapTarball, "-C", root, "--strip-components=1")
	extractCmd.Stdout = runner.LogWriter()
	extractCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(extractCmd); err != nil {
		return fmt.Errorf("failed to extract bootstrap: %w", err)
	}

	// Initialize pacman keyring
	runner.Logln("Initializing pacman keyring in image chroot...")
	keyringCmd := exec.Command("sudo", "chroot", root, "pacman-key", "--init")
	keyringCmd.Stdout = runner.LogWriter()
	keyringCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(keyringCmd); err != nil {
		return fmt.Errorf("failed to init keyring: %w", err)
	}

	populateCmd := exec.Command("sudo", "chroot", root, "pacman-key", "--populate", "archlinux")
	populateCmd.Stdout = runner.LogWriter()
	populateCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(populateCmd); err != nil {
		return fmt.Errorf("failed to populate keyring: %w", err)
	}

	// Enable mirrors
	mirrorlist := filepath.Join(root, "etc", "pacman.d", "mirrorlist")
	sedCmd := exec.Command("sudo", "sed", "-i", "s/^#Server/Server/", mirrorlist)
	if err := runner.RunCmd(sedCmd); err != nil {
		return fmt.Errorf("failed to enable mirrors: %w", err)
	}

	// Copy resolv.conf
	if _, err := os.Stat("/etc/resolv.conf"); err == nil {
		_ = copyFileWithSudo("/etc/resolv.conf", filepath.Join(root, "etc", "resolv.conf"))
	}

	return nil
}

// installImageTools installs QEMU and image-building utilities
func (b *Builder) installImageTools(root string) error {
	runner.Logln("Installing image-building tools...")

	// In this chroot setup, /proc/self/mounts does not always expose a usable "/"
	// entry, so pacman's CheckSpace can fail with a false ENOSPC. Disable it here.
	pacmanConf := filepath.Join(root, "etc", "pacman.conf")
	disableCheckSpaceCmd := exec.Command(
		"sudo", "sed", "-i", "-E", "s/^[[:space:]]*CheckSpace[[:space:]]*$/#CheckSpace/", pacmanConf,
	)
	disableCheckSpaceCmd.Stdout = runner.LogWriter()
	disableCheckSpaceCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(disableCheckSpaceCmd); err != nil {
		return fmt.Errorf("failed to update pacman.conf for image chroot: %w", err)
	}

	packages := []string{
		"qemu-user-static",
		"qemu-user-static-binfmt",
		"parted",               // Disk partitioning
		"e2fsprogs",            // ext4 filesystem (root partition)
		"arch-install-scripts", // For genfstab, arch-chroot
	}

	args := append([]string{"chroot", root, "pacman", "-Sy", "--noconfirm"}, packages...)
	installCmd := exec.Command("sudo", args...)
	installCmd.Stdout = runner.LogWriter()
	installCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(installCmd); err != nil {
		return fmt.Errorf("failed to install packages: %w", err)
	}

	// Register binfmt handlers (same as build chroot)
	if err := b.registerBinfmtInChroot(root); err != nil {
		runner.Logf("Warning: binfmt registration failed: %v\n", err)
	}

	return nil
}

// registerBinfmtInChroot reads QEMU binfmt config files and registers them
func (b *Builder) registerBinfmtInChroot(root string) error {
	runner.Logln("Registering QEMU binfmt handlers...")
	binfmtDir := filepath.Join(root, "usr", "lib", "binfmt.d")
	entries, err := os.ReadDir(binfmtDir)
	if err != nil {
		return fmt.Errorf("could not read binfmt.d directory: %w", err)
	}

	for _, entry := range entries {
		if !strings.HasPrefix(entry.Name(), "qemu-") || !strings.HasSuffix(entry.Name(), ".conf") {
			continue
		}
		confPath := filepath.Join(binfmtDir, entry.Name())
		runner.Logf("Processing %s...\n", entry.Name())

		data, err := os.ReadFile(confPath)
		if err != nil {
			runner.Logf("Warning: could not read %s: %v\n", entry.Name(), err)
			continue
		}

		// Process each line (conf files contain one registration string per line)
		lines := strings.Split(string(data), "\n")
		for _, line := range lines {
			line = strings.TrimSpace(line)
			// Skip empty lines and comments
			if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
				continue
			}

			// Each line should be a binfmt_misc registration string
			if !strings.HasPrefix(line, ":") {
				continue
			}

			// Parse and modify interpreter path to use absolute path to our static QEMU
			parts := strings.Split(line, ":")
			if len(parts) >= 7 {
				interpreterName := filepath.Base(parts[6])
				absStaticPath := filepath.Join(root, "usr", "bin", interpreterName)

				if _, err := os.Stat(absStaticPath); err == nil {
					parts[6] = absStaticPath
				} else {
					parts[6] = strings.Replace(parts[6], "-static", "", 1)
				}

				// Ensure the F flag is present
				if len(parts) >= 8 {
					if !strings.Contains(parts[7], "F") {
						parts[7] = parts[7] + "F"
					}
				} else {
					parts = append(parts, "F")
				}

				line = strings.Join(parts, ":")
			}

			// Write to /proc/sys/fs/binfmt_misc/register
			registerCmd := exec.Command("sudo", "sh", "-c", fmt.Sprintf("echo '%s' > /proc/sys/fs/binfmt_misc/register", line))
			registerCmd.Stdout = runner.LogWriter()
			registerCmd.Stderr = runner.LogWriter()
			if err := runner.RunCmd(registerCmd); err != nil {
				// Ignore errors - entry might already exist
				if len(parts) > 1 {
					runner.Logf("Note: %s already registered or error\n", parts[1])
				}
			} else {
				if len(parts) > 6 {
					runner.Logf("Registered: %s (using %s)\n", parts[1], parts[6])
				}
			}
		}
	}

	return nil
}

// InstallPackagesToRootfs installs ARM packages from cache into a rootfs directory
// This happens inside the image-build-chroot
func (b *Builder) InstallPackagesToRootfs(imageChrootRoot, rootfsPath string, packages []string, arch string) error {
	// De-duplicate to avoid duplicate targets in pacman.
	packages = uniqueStrings(packages)
	runner.Logf("Installing %d packages to rootfs...\n", len(packages))

	// Ensure rootfs directory exists on host
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", rootfsPath)); err != nil {
		return fmt.Errorf("failed to create rootfs dir: %w", err)
	}

	// Create var/lib/pacman inside rootfs to satisfy alpm
	dbPath := filepath.Join(rootfsPath, "var", "lib", "pacman")
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", dbPath)); err != nil {
		return fmt.Errorf("failed to create pacman db dir: %w", err)
	}

	// Prepare mount points inside the chroot
	internalRootfs := "/mnt/rootfs"
	internalCache := "/mnt/cache"
	hostRootfsMount := filepath.Join(imageChrootRoot, "mnt", "rootfs")
	hostCacheMount := filepath.Join(imageChrootRoot, "mnt", "cache")

	// Create mount points
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", hostRootfsMount)); err != nil {
		return fmt.Errorf("failed to create rootfs mount point: %w", err)
	}
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", hostCacheMount)); err != nil {
		return fmt.Errorf("failed to create cache mount point: %w", err)
	}

	// Bind mount rootfs
	if !isMountPoint(hostRootfsMount) {
		if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", rootfsPath, hostRootfsMount)); err != nil {
			return fmt.Errorf("failed to bind mount rootfs: %w", err)
		}
	}
	defer chroot.UnmountPathWithSudo(hostRootfsMount)

	// Bind mount the persistent per-arch distro download cache as the rootfs
	// pacman cachedir, so the base distro's OS packages aren't re-fetched on
	// every fresh rootfs build. (Was b.CacheDir — persistent but flat across
	// all arches and mixed with source downloads.)
	if !isMountPoint(hostCacheMount) {
		if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", b.DistroPkgCacheDir("arch", arch), hostCacheMount)); err != nil {
			return fmt.Errorf("failed to bind mount cache: %w", err)
		}
	}
	defer chroot.UnmountPathWithSudo(hostCacheMount)

	// Create pacman.conf for rootfs installation
	confContent := pacman.GenerateConfigContent(arch)

	// We write the config to a file accessible inside the chroot
	confPath := filepath.Join(imageChrootRoot, "tmp", "rootfs-pacman.conf")
	if err := os.WriteFile(confPath, []byte(confContent), 0644); err != nil {
		return fmt.Errorf("failed to write pacman config: %w", err)
	}

	// Identify local vs repo packages
	var repoPkgs []string
	var localPkgs []string

	// Locally-built packages are .feather in the per-arch store; everything
	// else comes from the base distro's repos (we sit on top of a distro,
	// not replace its package manager).
	storeArch := arch
	if arch == "armv7" {
		storeArch = "armv7h"
	}
	runner.Logln("Identifying package locations...")
	for _, pkg := range packages {
		pattern := filepath.Join(b.PackagesDir(), storeArch, pkg+"-*.feather")
		matches, err := filepath.Glob(pattern)
		if err != nil {
			runner.Logf("Glob error for %s: %v\n", pkg, err)
		}

		var selected string
		var selectedMtime time.Time
		for _, m := range matches {
			if featherPackageName(m) != pkg {
				continue
			}
			// Prefer the newest artifact by mtime (dotted versions don't
			// sort lexically, and stale artifacts may linger).
			fi, err := os.Stat(m)
			if err != nil {
				continue
			}
			if selected == "" || fi.ModTime().After(selectedMtime) {
				selected = m
				selectedMtime = fi.ModTime()
			}
		}

		if selected != "" {
			runner.Logf(" [LOCAL] %s -> %s\n", pkg, filepath.Base(selected))
			localPkgs = append(localPkgs, selected) // host path; ftr installs with --root
		} else {
			runner.Logf(" [REPO]  %s from distro repo\n", pkg)
			repoPkgs = append(repoPkgs, pkg)
		}
	}
	repoPkgs = uniqueStrings(repoPkgs)
	localPkgs = uniqueStrings(localPkgs)

	// Mount proc/sys/dev to rootfs so hooks work
	// effectively chrooting into rootfs requires these
	targetProc := filepath.Join(rootfsPath, "proc")
	targetSys := filepath.Join(rootfsPath, "sys")
	targetDev := filepath.Join(rootfsPath, "dev")
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", targetProc, targetSys, targetDev)); err != nil {
		return fmt.Errorf("failed to create pseudo-fs mount points: %w", err)
	}

	if !isMountPoint(targetProc) {
		if err := runner.RunCmd(exec.Command("sudo", "mount", "-t", "proc", "proc", targetProc)); err != nil {
			return fmt.Errorf("failed to mount proc in rootfs: %w", err)
		}
	}
	defer chroot.UnmountPathWithSudo(targetProc)

	if !isMountPoint(targetSys) {
		if err := runner.RunCmd(exec.Command("sudo", "mount", "-t", "sysfs", "sys", targetSys)); err != nil {
			return fmt.Errorf("failed to mount sysfs in rootfs: %w", err)
		}
	}
	defer chroot.UnmountPathWithSudo(targetSys)

	if !isMountPoint(targetDev) {
		if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", "/dev", targetDev)); err != nil {
			return fmt.Errorf("failed to bind mount /dev in rootfs: %w", err)
		}
		// Fully isolate rootfs /dev from host propagation.
		if err := runner.RunCmd(exec.Command("sudo", "mount", "--make-rprivate", targetDev)); err != nil {
			return fmt.Errorf("failed to mark rootfs /dev mount rprivate: %w", err)
		}
	}
	defer chroot.UnmountPathWithSudo(targetDev)

	commonArgs := []string{
		"chroot", imageChrootRoot,
		"pacman", "-r", internalRootfs,
		"--noconfirm",
		"--ask=4",
		"--needed",
		"--config", "/tmp/rootfs-pacman.conf",
		"--cachedir", internalCache,
	}

	// 1. Synchronize databases
	syncArgs := append(commonArgs, "-Sy")
	if err := runner.RunCmd(exec.Command("sudo", syncArgs...)); err != nil {
		return fmt.Errorf("failed to sync pacman db: %w", err)
	}

	// 2. Install Repo Packages
	if len(repoPkgs) > 0 {
		installArgs := append(commonArgs, "-S")
		installArgs = append(installArgs, repoPkgs...)
		cmd := exec.Command("sudo", installArgs...)
		cmd.Stdout = runner.LogWriter()
		cmd.Stderr = runner.LogWriter()
		if err := runner.RunCmd(cmd); err != nil {
			return fmt.Errorf("failed to install repo packages: %w", err)
		}
	}

	// 3. Install our own (locally-built) packages via feather, overlaying
	// each .feather onto the rootfs at its layout prefix. DB sandboxed
	// under the rootfs via FTR_DB_ROOT.
	if len(localPkgs) > 0 {
		ftr, err := FtrBinary()
		if err != nil {
			return err
		}
		dbRoot := filepath.Join(rootfsPath, "var", "lib", "feather")
		for _, fea := range localPkgs {
			cmd := exec.Command("sudo", "env", "FTR_DB_ROOT="+dbRoot,
				ftr, "install", "--root", rootfsPath, "--allow-unsigned", fea)
			cmd.Stdout = runner.LogWriter()
			cmd.Stderr = runner.LogWriter()
			if err := runner.RunCmd(cmd); err != nil {
				return fmt.Errorf("failed to ftr-install %s: %w", filepath.Base(fea), err)
			}
		}
	}

	if err := ensureRootfsLoaderSymlink(rootfsPath, arch); err != nil {
		return err
	}

	runner.Logln("Packages installed to rootfs successfully")
	return nil
}

func ensureRootfsLoaderSymlink(rootfsPath, arch string) error {
	loader := ""
	switch arch {
	case "aarch64":
		loader = "ld-linux-aarch64.so.1"
	case "armv7", "armv7h", "armhf":
		loader = "ld-linux-armhf.so.3"
	default:
		return nil
	}

	src := filepath.Join(rootfsPath, "usr", "lib", loader)
	if _, err := os.Stat(src); err != nil {
		// If loader is not present in /usr/lib, nothing to link.
		return nil
	}

	libDir := filepath.Join(rootfsPath, "lib")
	dst := filepath.Join(libDir, loader)
	if _, err := os.Lstat(dst); err == nil {
		return nil
	}

	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", libDir)); err != nil {
		return fmt.Errorf("failed to create rootfs lib dir for loader symlink: %w", err)
	}
	if err := runner.RunCmd(exec.Command("sudo", "ln", "-s", "/usr/lib/"+loader, dst)); err != nil {
		return fmt.Errorf("failed to create loader symlink %s -> /usr/lib/%s: %w", dst, loader, err)
	}
	runner.Logf("Created rootfs loader symlink: %s -> /usr/lib/%s\n", dst, loader)
	return nil
}

func uniqueStrings(in []string) []string {
	seen := make(map[string]struct{}, len(in))
	out := make([]string, 0, len(in))
	for _, v := range in {
		v = strings.TrimSpace(v)
		if v == "" {
			continue
		}
		if _, ok := seen[v]; ok {
			continue
		}
		seen[v] = struct{}{}
		out = append(out, v)
	}
	return out
}

func isMountPoint(path string) bool {
	cmd := exec.Command("mountpoint", "-q", path)
	return cmd.Run() == nil
}

// CreateDiskImage creates a bootable disk image from a populated rootfs
// This creates partitions, filesystems, and copies the rootfs contents
func (b *Builder) CreateDiskImage(imageChrootRoot, rootfsPath, outputPath string, sizeM int, legacyRootfsExt4 bool) error {
	runner.Logf("Creating disk image: %s (%dMB)\n", outputPath, sizeM)

	// Create empty image file
	ddCmd := exec.Command("dd", "if=/dev/zero", fmt.Sprintf("of=%s", outputPath), "bs=1M", fmt.Sprintf("count=%d", sizeM))
	ddCmd.Stdout = runner.LogWriter()
	ddCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(ddCmd); err != nil {
		return fmt.Errorf("failed to create image file: %w", err)
	}

	// Set up loop device
	loopDevice, err := b.setupLoopDevice(outputPath)
	if err != nil {
		return fmt.Errorf("failed to setup loop device: %w", err)
	}
	defer b.detachLoopDevice(loopDevice)

	// Create partition table and partitions
	if err := b.partitionDisk(loopDevice); err != nil {
		return fmt.Errorf("failed to partition disk: %w", err)
	}

	// Format partitions
	bootPart := loopDevice + "p1"
	rootPart := loopDevice + "p2"
	if err := b.formatPartitions(bootPart, rootPart, legacyRootfsExt4); err != nil {
		return fmt.Errorf("failed to format partitions: %w", err)
	}

	// Mount and copy rootfs
	if err := b.copyRootfsToImage(rootfsPath, rootPart, bootPart); err != nil {
		return fmt.Errorf("failed to copy rootfs: %w", err)
	}

	runner.Logf("Disk image created successfully: %s\n", outputPath)
	return nil
}

// featherPackageName extracts the package name from a store filename
// <name>-<version>-<rel>-<arch>.feather. The version starts with a digit,
// so the name is everything before the first "-<digit>".
func featherPackageName(path string) string {
	base := strings.TrimSuffix(filepath.Base(path), ".feather")
	for i := 0; i+1 < len(base); i++ {
		if base[i] == '-' && base[i+1] >= '0' && base[i+1] <= '9' {
			return base[:i]
		}
	}
	return base
}

// FtrBinary locates the feather (ftr) CLI. ftr is NOT a host dependency users
// must install: it's a sibling binary in the monorepo (feather/ftr) and is
// bundled beside peacock-builder in distribution. Resolution order:
//  1. $PEACOCK_FTR                       (explicit override)
//  2. bundled beside the executable      (AppImage/dist ships ftr next to us)
//  3. <monorepo>/feather/ftr             (dev: found by walking up from the exe)
//  4. PATH                               (a user-installed ftr still works)
//  5. ../feather/ftr or feather/ftr      (running from within the repo)
func FtrBinary() (string, error) {
	if p := strings.TrimSpace(os.Getenv("PEACOCK_FTR")); p != "" {
		return p, nil
	}
	if exe, err := os.Executable(); err == nil {
		dir := filepath.Dir(exe)
		// Bundled beside the binary (dist/AppImage cp's ftr into usr/bin).
		if p, ok := ftrExecCandidate(filepath.Join(dir, "ftr")); ok {
			return p, nil
		}
		// Dev tree: peacock-builder builds under Peacock/cmd/peacock-builder/
		// build/bin/, and feather/ftr is a sibling of Peacock/ at the monorepo
		// root. Walk up looking for a feather/ftr sibling rather than hardcoding
		// the nesting depth.
		for i := 0; i < 8; i++ {
			if p, ok := ftrExecCandidate(filepath.Join(dir, "feather", "ftr")); ok {
				return p, nil
			}
			parent := filepath.Dir(dir)
			if parent == dir {
				break
			}
			dir = parent
		}
	}
	if p, err := exec.LookPath("ftr"); err == nil {
		return p, nil
	}
	for _, c := range []string{"../feather/ftr", "feather/ftr"} {
		if p, ok := ftrExecCandidate(c); ok {
			return p, nil
		}
	}
	return "", fmt.Errorf("feather (ftr) not found: set PEACOCK_FTR, put ftr on PATH, or build it (make -C feather)")
}

// ftrExecCandidate returns an absolute path to p if it is an executable regular
// file, else ok=false.
func ftrExecCandidate(p string) (string, bool) {
	fi, err := os.Stat(p)
	if err != nil || fi.IsDir() || fi.Mode()&0o111 == 0 {
		return "", false
	}
	if abs, err := filepath.Abs(p); err == nil {
		return abs, true
	}
	return p, true
}

// setupLoopDevice attaches the image file to a loop device
func (b *Builder) setupLoopDevice(imagePath string) (string, error) {
	cmd := exec.Command("sudo", "losetup", "--find", "--show", "--partscan", imagePath)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("losetup failed: %w: %s", err, string(output))
	}
	loopDevice := strings.TrimSpace(string(output))
	runner.Logf("Attached loop device: %s\n", loopDevice)
	return loopDevice, nil
}

// detachLoopDevice detaches the loop device
func (b *Builder) detachLoopDevice(loopDevice string) {
	cmd := exec.Command("sudo", "losetup", "-d", loopDevice)
	_ = cmd.Run()
}

// partitionDisk creates GPT partition table with boot and root partitions
func (b *Builder) partitionDisk(device string) error {
	runner.Logln("Creating partition table...")

	commands := [][]string{
		{"sudo", "parted", "-s", device, "mklabel", "gpt"},
		// Keep BOOT large enough for modern kernels + initramfs (desktop profiles can exceed 100MiB).
		{"sudo", "parted", "-s", device, "mkpart", "boot", "ext2", "1MiB", "513MiB"},
		{"sudo", "parted", "-s", device, "mkpart", "root", "ext4", "513MiB", "100%"},
		{"sudo", "parted", "-s", device, "set", "1", "boot", "on"},
	}

	for _, args := range commands {
		cmd := exec.Command(args[0], args[1:]...)
		cmd.Stdout = runner.LogWriter()
		cmd.Stderr = runner.LogWriter()
		if err := runner.RunCmd(cmd); err != nil {
			return fmt.Errorf("parted command failed: %w", err)
		}
	}

	return nil
}

// formatPartitions creates FAT32 boot and ext4 root filesystems
func (b *Builder) formatPartitions(bootPart, rootPart string, legacyRootfsExt4 bool) error {
	runner.Logln("Formatting partitions...")

	// Format boot partition as ext2 so lk2nd can mount/read extlinux directly.
	bootCmd := exec.Command("sudo", "mkfs.ext2", "-F", "-L", "BOOT", bootPart)
	bootCmd.Stdout = runner.LogWriter()
	bootCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(bootCmd); err != nil {
		return fmt.Errorf("failed to format boot partition: %w", err)
	}

	rootArgs := []string{
		"mkfs.ext4",
		"-L", "ROOT",
	}
	// Some downstream kernels cannot mount modern ext4 defaults.
	// When requested by a device quirk, keep legacy-compatible features.
	if legacyRootfsExt4 {
		rootArgs = append(rootArgs, "-O", "^metadata_csum,^metadata_csum_seed,^64bit,^orphan_file")
	}
	rootArgs = append(rootArgs, "-E", "lazy_itable_init=0,lazy_journal_init=0", rootPart)
	rootCmd := exec.Command("sudo", rootArgs...)
	rootCmd.Stdout = runner.LogWriter()
	rootCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(rootCmd); err != nil {
		return fmt.Errorf("failed to format root partition: %w", err)
	}

	return nil
}

// copyRootfsToImage mounts partitions and copies rootfs contents
func (b *Builder) copyRootfsToImage(rootfsPath, rootPart, bootPart string) error {
	runner.Logln("Copying rootfs to image...")

	// Create mount points
	mountRoot := filepath.Join(os.TempDir(), "peacock-img-root")
	mountBoot := filepath.Join(mountRoot, "boot")

	if err := os.MkdirAll(mountBoot, 0755); err != nil {
		return fmt.Errorf("failed to create mount points: %w", err)
	}
	defer os.RemoveAll(mountRoot)

	// Mount root partition
	if err := runner.RunCmd(exec.Command("sudo", "mount", rootPart, mountRoot)); err != nil {
		return fmt.Errorf("failed to mount root partition: %w", err)
	}
	defer runner.RunCmd(exec.Command("sudo", "umount", mountRoot))

	// Create boot mount point inside root partition
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", mountBoot)); err != nil {
		return fmt.Errorf("failed to create boot mount point: %w", err)
	}

	// Mount boot partition
	if err := runner.RunCmd(exec.Command("sudo", "mount", bootPart, mountBoot)); err != nil {
		return fmt.Errorf("failed to mount boot partition: %w", err)
	}
	defer runner.RunCmd(exec.Command("sudo", "umount", mountBoot))

	// Copy rootfs contents (skip pseudo filesystems)
	copyCmd := exec.Command(
		"sudo", "rsync",
		"-aAX", "--one-file-system",
		"--exclude=/proc/*",
		"--exclude=/sys/*",
		"--exclude=/dev/*",
		"--exclude=/run/*",
		"--exclude=/tmp/*",
		rootfsPath+"/", mountRoot+"/",
	)
	copyCmd.Stdout = runner.LogWriter()
	copyCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(copyCmd); err != nil {
		return fmt.Errorf("failed to copy rootfs: %w", err)
	}

	// CRITICAL: Sync filesystem before unmount to prevent corruption
	runner.Logln("Syncing filesystem...")
	if err := runner.RunCmd(exec.Command("sync")); err != nil {
		return fmt.Errorf("failed to sync filesystem: %w", err)
	}

	runner.Logln("Rootfs copied successfully")
	return nil
}
