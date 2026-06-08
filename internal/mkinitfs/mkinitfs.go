package mkinitfs

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"text/template"

	"peacock/internal/runner"
)

// InitConfig holds configuration for the init script validation
type InitConfig struct {
	InitSystem        string // "openrc" or "systemd"
	RootLabel         string // Filesystem label for root partition (e.g., "ROOT")
	BusyboxPath       string // Path to static busybox binary
	Resize2fsPath     string // Path to resize2fs binary (optional, will try to find if empty)
	SplashPath        string // Path to peacock-splash binary (optional)
	RefresherPath     string // Path to msm-fb-refresher binary (optional)
	Architecture      string // Target arch (e.g., "armv7h", "aarch64", "x86_64")
	DeviceName        string // Device codename (e.g., "samsung-jflte")
	EnableS4CameraLED bool   // Enable S4-specific camera LED debug flashes in initramfs
	// UtilLinuxBuildDir points at the staged util-linux port build directory
	// (sbin/, bin/, usr/bin/, lib/, usr/lib/). When set, the initramfs builder
	// harvests losetup/partx/blkid/lsblk + shared libs from here. Falls back to
	// a no-op when empty (e.g. legacy callers or partial builds).
	UtilLinuxBuildDir string
	// Lvm2BuildDir points at the staged lvm2 port build directory which
	// provides sbin/dmsetup and libdevmapper. When set, the initramfs builder
	// prefers this over host paths for the dmsetup binary + its lib search.
	Lvm2BuildDir string
	// InitramfsToolsBuildDir points at the staged peacock-initramfs-tools
	// port build directory. When set, the initramfs builder prefers
	// <dir>/usr/lib/peacock/init.sh.in (and the sibling init-wrapper.go.in,
	// subparts-mount.sh) over the in-tree assets/initramfs/ fallback copies.
	// Empty falls through to assets/initramfs/, then to legacy prp/ paths.
	InitramfsToolsBuildDir string
}

// Compile-time toggle for S4 camera LED debug flashes in initramfs.
// Keep disabled by default; set to true only when explicitly debugging boot stages.
const enableS4CameraLED = false

// initramfsToolsAssetCandidates returns the ordered list of candidate file
// paths for an asset shipped by the peacock-initramfs-tools port. The lookup
// order is:
//
//  1. The port's staged build dir (when InitramfsToolsBuildDir is set),
//     under usr/lib/peacock/<asset> — this is the canonical location that
//     pacman installs to.
//  2. The in-tree assets/initramfs/<asset> — the fallback for fresh
//     checkouts that haven't built the port yet.
//  3. Legacy prp/initramfs/rootfs/usr/lib/prp/<asset> — kept so out-of-tree
//     builds that still hold a PRP checkout next to the Peacock source
//     keep working.
//
// Callers use findFirstExisting on the result.
func initramfsToolsAssetCandidates(cfg InitConfig, asset string) []string {
	var out []string
	if cfg.InitramfsToolsBuildDir != "" {
		out = append(out,
			filepath.Join(cfg.InitramfsToolsBuildDir, "usr", "lib", "peacock", asset),
			filepath.Join(cfg.InitramfsToolsBuildDir, "stage", "usr", "lib", "peacock", asset),
		)
	}
	out = append(out,
		filepath.Join("assets", "initramfs", asset),
		filepath.Join("prp", "initramfs", "rootfs", "usr", "lib", "prp", asset),
	)
	return out
}

// loadInitTemplate reads the init script template from the first available
// candidate (port build dir, in-tree assets, legacy prp tree).
func loadInitTemplate(cfg InitConfig) (string, string, error) {
	src := findFirstExisting(initramfsToolsAssetCandidates(cfg, "init.sh.in"))
	if src == "" {
		return "", "", fmt.Errorf("init.sh.in not found in port build dir, assets/initramfs/, or legacy prp tree")
	}
	body, err := os.ReadFile(src)
	if err != nil {
		return "", "", fmt.Errorf("failed to read init template %s: %w", src, err)
	}
	return string(body), src, nil
}

// loadInitWrapperSource reads the /init wrapper Go source from the first
// available candidate.
func loadInitWrapperSource(cfg InitConfig) (string, string, error) {
	src := findFirstExisting(initramfsToolsAssetCandidates(cfg, "init-wrapper.go.in"))
	if src == "" {
		return "", "", fmt.Errorf("init-wrapper.go.in not found in port build dir, assets/initramfs/, or legacy prp tree")
	}
	body, err := os.ReadFile(src)
	if err != nil {
		return "", "", fmt.Errorf("failed to read init wrapper source %s: %w", src, err)
	}
	return string(body), src, nil
}

// GenerateInitScript writes the init script to the target path
func GenerateInitScript(path string, cfg InitConfig) error {
	body, src, err := loadInitTemplate(cfg)
	if err != nil {
		return err
	}
	tmpl, err := template.New("init").Parse(body)
	if err != nil {
		return fmt.Errorf("failed to parse init template %s: %w", src, err)
	}

	var buf bytes.Buffer
	if err := tmpl.Execute(&buf, cfg); err != nil {
		return fmt.Errorf("failed to execute init template %s: %w", src, err)
	}

	if err := os.WriteFile(path, buf.Bytes(), 0755); err != nil {
		return fmt.Errorf("failed to write init script: %w", err)
	}

	return nil
}

func buildInitWrapper(outPath string, cfg InitConfig) error {
	arch := cfg.Architecture
	goarch := ""
	goarm := ""
	switch arch {
	case "armv7h":
		goarch = "arm"
		goarm = "7"
	case "armv7":
		goarch = "arm"
		goarm = "7"
	case "aarch64":
		goarch = "arm64"
	case "x86_64":
		goarch = "amd64"
	default:
		return fmt.Errorf("unsupported architecture for init wrapper: %s", arch)
	}

	wrapperSrc, _, err := loadInitWrapperSource(cfg)
	if err != nil {
		return err
	}

	tmpDir, err := os.MkdirTemp("", "peacock-init-wrapper-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir)

	srcPath := filepath.Join(tmpDir, "main.go")
	if err := os.WriteFile(srcPath, []byte(wrapperSrc), 0644); err != nil {
		return fmt.Errorf("failed to write init wrapper source: %w", err)
	}

	cmd := exec.Command("go", "build", "-trimpath", "-ldflags", "-s -w", "-o", outPath, srcPath)
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	cmd.Env = append(os.Environ(), "CGO_ENABLED=0", "GOOS=linux", "GOARCH="+goarch)
	if goarm != "" {
		cmd.Env = append(cmd.Env, "GOARM="+goarm)
	}
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("failed to build init wrapper: %w", err)
	}

	return nil
}

func findFirstExisting(paths []string) string {
	for _, p := range paths {
		if p == "" {
			continue
		}
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return ""
}

func appendUniquePath(paths []string, p string) []string {
	if p == "" {
		return paths
	}
	clean := filepath.Clean(p)
	for _, existing := range paths {
		if existing == clean {
			return paths
		}
	}
	return append(paths, clean)
}

// runtimeVendorCandidates returns directories whose sbin/, bin/, usr/bin/,
// lib/, usr/lib/ trees are copied verbatim into the initramfs to provide rich
// util-linux tooling (losetup/partx/blkid/lsblk + shared libs) for nested-root
// probing. Historically this consumed `prp/vendor/<device>/rootfs-runtime`
// when PRP was vendored in-tree; that path is gone since the PRP split, so the
// canonical source is now the util-linux port build directory passed in via
// InitConfig.UtilLinuxBuildDir.
func runtimeVendorCandidates(utilLinuxBuildDir string) []string {
	var out []string
	if utilLinuxBuildDir != "" {
		out = appendUniquePath(out, utilLinuxBuildDir)
		// Some package layouts stage payloads under a "stage" subdir.
		out = appendUniquePath(out, filepath.Join(utilLinuxBuildDir, "stage"))
	}
	return out
}

// runtimeStageCandidates used to enumerate `prp/out/<device>/initramfs-stage`
// directories produced by a vendored PRP build. With PRP split out and no
// in-tree analogue yet, this returns nothing — keeping the function around
// so the rest of the dmsetup/library-search wiring can be extended cheaply
// when a future port emits an initramfs-stage tree.
func runtimeStageCandidates(deviceName string) []string {
	_ = deviceName
	return nil
}

func copyFileOrSymlink(src, dst string) error {
	info, err := os.Lstat(src)
	if err != nil {
		return err
	}

	if info.Mode()&os.ModeSymlink != 0 {
		target, err := os.Readlink(src)
		if err != nil {
			return err
		}
		_ = os.RemoveAll(dst)
		return os.Symlink(target, dst)
	}

	if !info.Mode().IsRegular() {
		return nil
	}

	content, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	mode := info.Mode() & 0o777
	if mode == 0 {
		mode = 0o644
	}
	return os.WriteFile(dst, content, mode)
}

func copyTree(srcRoot, dstRoot string) error {
	srcInfo, err := os.Stat(srcRoot)
	if err != nil {
		return err
	}
	if !srcInfo.IsDir() {
		return fmt.Errorf("source is not a directory: %s", srcRoot)
	}
	if err := os.MkdirAll(dstRoot, 0755); err != nil {
		return err
	}

	return filepath.Walk(srcRoot, func(path string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		rel, err := filepath.Rel(srcRoot, path)
		if err != nil {
			return err
		}
		if rel == "." {
			return nil
		}

		dst := filepath.Join(dstRoot, rel)
		if info.IsDir() {
			return os.MkdirAll(dst, info.Mode()&0o777)
		}
		if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
			return err
		}
		return copyFileOrSymlink(path, dst)
	})
}

// Build creates the initramfs cpio structure (Simulated for Prototype)
// In a real implementation:
// 1. Create temp dir
// 2. Copy busybox binary
// 3. GenerateInitScript
// 4. CPIO archive it 'find . | cpio -o -H newc > initramfs.cpio'
func Build(output string, cfg InitConfig) error {
	fmt.Printf("Generating init script for %s...\n", cfg.InitSystem)
	cfg.EnableS4CameraLED = enableS4CameraLED

	// 1. Create temp dir for initramfs structure
	tmpDir, err := os.MkdirTemp("", "peacock-initramfs-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir) // Clean up

	// 2. Create base directories
	for _, dir := range []string{"proc", "sys", "dev", "run", "tmp", "etc", "usr", "lib"} {
		if err := os.MkdirAll(filepath.Join(tmpDir, dir), 0755); err != nil {
			return fmt.Errorf("failed to create initramfs dir %s: %w", dir, err)
		}
	}

	// 3. Copy Busybox
	binDir := filepath.Join(tmpDir, "bin")
	if err := os.MkdirAll(binDir, 0755); err != nil {
		return err
	}

	// Copy busybox binary
	bbDest := filepath.Join(binDir, "busybox")
	input, err := os.ReadFile(cfg.BusyboxPath)
	if err != nil {
		return fmt.Errorf("failed to read busybox binary: %w", err)
	}
	if err := os.WriteFile(bbDest, input, 0755); err != nil {
		return fmt.Errorf("failed to write busybox binary: %w", err)
	}

	// Create symlinks for busybox applets (like /bin/sh, /bin/mount, etc.)
	// This is CRITICAL - without /bin/sh symlink, the kernel can't execute #!/bin/sh scripts!
	commonApplets := []string{
		"sh", "ash", "mount", "umount", "mknod", "mkdir", "rmdir",
		"cat", "ls", "cp", "mv", "rm", "ln", "chmod", "chown",
		"echo", "printf", "test", "[", "sleep", "usleep",
		"grep", "sed", "awk", "cut", "sort", "uniq", "head", "tail",
		"find", "xargs", "tar", "gzip", "gunzip", "cpio",
		"dd", "sync", "switch_root", "reboot", "poweroff", "halt",
		"true", "false", "date", "touch", "stat", "df", "du",
		"ps", "kill", "killall", "pidof", "top",
	}

	for _, applet := range commonApplets {
		symlinkPath := filepath.Join(binDir, applet)
		// Create relative symlink: bin/sh -> bin/busybox
		if err := os.Symlink("busybox", symlinkPath); err != nil {
			// Don't fail if symlink already exists or can't be created
			fmt.Printf("Warning: failed to create symlink %s: %v\n", applet, err)
		}
	}

	// Copy resize2fs binary (for rootfs resizing)
	sbinDir := filepath.Join(tmpDir, "sbin")
	if err := os.MkdirAll(sbinDir, 0755); err != nil {
		return err
	}

	resize2fsPath := cfg.Resize2fsPath
	if resize2fsPath == "" {
		// Try to find resize2fs in common locations
		for _, path := range []string{"/usr/sbin/resize2fs", "/sbin/resize2fs", "/usr/bin/resize2fs"} {
			if _, err := os.Stat(path); err == nil {
				resize2fsPath = path
				break
			}
		}
	}

	if resize2fsPath != "" {
		resize2fsDest := filepath.Join(sbinDir, "resize2fs")
		resize2fsInput, err := os.ReadFile(resize2fsPath)
		if err != nil {
			return fmt.Errorf("failed to read resize2fs binary: %w", err)
		}
		if err := os.WriteFile(resize2fsDest, resize2fsInput, 0755); err != nil {
			return fmt.Errorf("failed to write resize2fs binary: %w", err)
		}
	} else {
		fmt.Printf("Warning: resize2fs not found, rootfs resize will be skipped\n")
	}

	// Copy runtime userspace from the util-linux port build directory when
	// available. With 512MiB BOOT, we can carry richer util-linux tooling
	// (losetup/partx/blkid/lsblk + shared libs) for nested root probing.
	runtimeRoot := findFirstExisting(runtimeVendorCandidates(cfg.UtilLinuxBuildDir))
	stageRoots := runtimeStageCandidates(cfg.DeviceName)
	if runtimeRoot != "" {
		type runtimeCopy struct {
			srcRel string
			dstRel string
		}
		for _, item := range []runtimeCopy{
			{srcRel: "sbin", dstRel: "sbin"},
			{srcRel: "bin", dstRel: "bin"},
			{srcRel: filepath.Join("usr", "bin"), dstRel: filepath.Join("usr", "bin")},
			{srcRel: "lib", dstRel: "lib"},
			{srcRel: filepath.Join("usr", "lib"), dstRel: filepath.Join("usr", "lib")},
		} {
			srcDir := filepath.Join(runtimeRoot, item.srcRel)
			if _, err := os.Stat(srcDir); err != nil {
				continue
			}
			dstDir := filepath.Join(tmpDir, item.dstRel)
			if err := copyTree(srcDir, dstDir); err != nil {
				return fmt.Errorf("failed to copy runtime tree %s -> %s: %w", srcDir, dstDir, err)
			}
		}
	}

	// Locate dmsetup. Prefer the lvm2 port build directory (canonical source);
	// fall back to the util-linux runtime root (unlikely to have it, but keep
	// the legacy shape), then any stage roots, then host paths so dev builds
	// without the lvm2 port still produce a usable cpio.
	dmsetupCandidates := []string{}
	if cfg.Lvm2BuildDir != "" {
		dmsetupCandidates = append(dmsetupCandidates,
			filepath.Join(cfg.Lvm2BuildDir, "sbin", "dmsetup"),
			filepath.Join(cfg.Lvm2BuildDir, "stage", "sbin", "dmsetup"),
		)
	}
	if runtimeRoot != "" {
		dmsetupCandidates = append(dmsetupCandidates, filepath.Join(runtimeRoot, "sbin", "dmsetup"))
	}
	for _, stage := range stageRoots {
		dmsetupCandidates = append(dmsetupCandidates, filepath.Join(stage, "sbin", "dmsetup"))
	}
	dmsetupCandidates = append(dmsetupCandidates,
		"/sbin/dmsetup",
		"/usr/sbin/dmsetup",
		"/usr/bin/dmsetup",
		"/bin/dmsetup",
	)
	dmsetupPath := findFirstExisting(dmsetupCandidates)
	if dmsetupPath != "" {
		dmsetupDest := filepath.Join(sbinDir, "dmsetup")
		if _, err := os.Stat(dmsetupDest); err != nil {
			dmsetupInput, err := os.ReadFile(dmsetupPath)
			if err != nil {
				return fmt.Errorf("failed to read dmsetup binary: %w", err)
			}
			if err := os.WriteFile(dmsetupDest, dmsetupInput, 0755); err != nil {
				return fmt.Errorf("failed to write dmsetup binary: %w", err)
			}
		}

		libDir := filepath.Join(tmpDir, "lib")
		if err := os.MkdirAll(libDir, 0755); err != nil {
			return err
		}

		libSearchDirs := []string{}
		if cfg.Lvm2BuildDir != "" {
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "usr", "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "stage", "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "stage", "usr", "lib"))
		}
		if runtimeRoot != "" {
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(runtimeRoot, "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(runtimeRoot, "usr", "lib"))
		}
		for _, stage := range stageRoots {
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(stage, "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(stage, "usr", "lib"))
		}
		libSearchDirs = appendUniquePath(libSearchDirs, "/lib")
		libSearchDirs = appendUniquePath(libSearchDirs, "/usr/lib")
		libSearchDirs = appendUniquePath(libSearchDirs, "/lib/arm-linux-gnueabihf")
		libSearchDirs = appendUniquePath(libSearchDirs, "/usr/lib/arm-linux-gnueabihf")
		requiredLibs := []string{
			"ld-linux-armhf.so.3",
			"libdevmapper.so.1.02",
			"libfdisk.so.1",
			"libfdisk.so.1.1.0",
			"libblkid.so.1",
			"libblkid.so.1.1.0",
			"libsmartcols.so.1",
			"libsmartcols.so.1.1.0",
			"libuuid.so.1",
			"libuuid.so.1.3.0",
			"libm.so.6",
			"libgcc_s.so.1",
			"libc.so.6",
		}
		for _, libName := range requiredLibs {
			var src string
			for _, d := range libSearchDirs {
				candidate := filepath.Join(d, libName)
				if _, err := os.Stat(candidate); err == nil {
					src = candidate
					break
				}
			}
			if src == "" {
				continue
			}
			dst := filepath.Join(libDir, libName)
			if _, err := os.Stat(dst); err == nil {
				continue
			}
			libInput, err := os.ReadFile(src)
			if err != nil {
				return fmt.Errorf("failed to read %s: %w", src, err)
			}
			if err := os.WriteFile(dst, libInput, 0755); err != nil {
				return fmt.Errorf("failed to write %s: %w", libName, err)
			}
		}
	} else {
		fmt.Printf("Warning: dmsetup not found, PRP-style dm-linear root probing will be unavailable\n")
	}

	// Copy peacock-splash binary (for framebuffer splash screen)
	if cfg.SplashPath != "" {
		splashDest := filepath.Join(binDir, "peacock-splash")
		splashInput, err := os.ReadFile(cfg.SplashPath)
		if err != nil {
			return fmt.Errorf("failed to read peacock-splash binary: %w", err)
		}
		if err := os.WriteFile(splashDest, splashInput, 0755); err != nil {
			return fmt.Errorf("failed to write peacock-splash binary: %w", err)
		}
	}

	// Optional handoff flare image used by initramfs right before root handover.
	conspiracySrc := findFirstExisting([]string{
		filepath.Join("conspiracy.png"),
		filepath.Join("assets", "conspiracy.png"),
		filepath.Join("prp", "assets", "conspiracy.png"),
	})
	if conspiracySrc != "" {
		conspiracyDir := filepath.Join(tmpDir, "etc", "peacock")
		if err := os.MkdirAll(conspiracyDir, 0755); err != nil {
			return fmt.Errorf("failed to create conspiracy image dir: %w", err)
		}
		conspiracyInput, err := os.ReadFile(conspiracySrc)
		if err != nil {
			return fmt.Errorf("failed to read conspiracy image: %w", err)
		}
		conspiracyDst := filepath.Join(conspiracyDir, "conspiracy.png")
		if err := os.WriteFile(conspiracyDst, conspiracyInput, 0644); err != nil {
			return fmt.Errorf("failed to write conspiracy image: %w", err)
		}
	}

	// Install the canonical Peacock sub-partition mount shell library into the
	// cpio at /usr/lib/peacock/subparts-mount.sh. The embedded init script
	// sources this file and calls into mount_subparts /
	// setup_subparts_root_dev — there is no longer an inline fallback.
	//
	// Lookup order mirrors initramfsToolsAssetCandidates: port build dir
	// first (canonical install path once peacock-initramfs-tools is built),
	// then in-tree assets/initramfs/, then legacy prp/ tree.
	subpartsSrc := findFirstExisting(initramfsToolsAssetCandidates(cfg, "subparts-mount.sh"))
	if subpartsSrc != "" {
		subpartsDir := filepath.Join(tmpDir, "usr", "lib", "peacock")
		if err := os.MkdirAll(subpartsDir, 0755); err != nil {
			return fmt.Errorf("failed to create subparts-mount dir: %w", err)
		}
		subpartsContent, err := os.ReadFile(subpartsSrc)
		if err != nil {
			return fmt.Errorf("failed to read subparts-mount.sh: %w", err)
		}
		subpartsDst := filepath.Join(subpartsDir, "subparts-mount.sh")
		if err := os.WriteFile(subpartsDst, subpartsContent, 0755); err != nil {
			return fmt.Errorf("failed to write subparts-mount.sh: %w", err)
		}
	} else {
		fmt.Printf("Warning: subparts-mount.sh not found in assets/initramfs/ or prp/initramfs/; the initramfs sub-partition fallback will be unavailable\n")
	}

	// Copy msm-fb-refresher binary (for MSM framebuffer refresh loop)
	if cfg.RefresherPath != "" {
		refresherDest := filepath.Join(binDir, "msm-fb-refresher")
		refresherInput, err := os.ReadFile(cfg.RefresherPath)
		if err != nil {
			return fmt.Errorf("failed to read msm-fb-refresher binary: %w", err)
		}
		if err := os.WriteFile(refresherDest, refresherInput, 0755); err != nil {
			return fmt.Errorf("failed to write msm-fb-refresher binary: %w", err)
		}
	}

	// 4. Generate Init Script
	initScriptPath := filepath.Join(tmpDir, "init.sh")
	if err := GenerateInitScript(initScriptPath, cfg); err != nil {
		return err
	}

	// 4b. Build binary /init wrapper so kernels without BINFMT_SCRIPT can boot
	initPath := filepath.Join(tmpDir, "init")
	if err := buildInitWrapper(initPath, cfg); err != nil {
		return err
	}

	// 5. Create CPIO archive (newc format) and compress with gzip
	// Use find . | cpio -o -H newc | gzip to create initramfs.cpio.gz
	findCmd := exec.Command("find", ".")
	findCmd.Dir = tmpDir
	findCmd.Stderr = runner.LogWriter()

	cpioCmd := exec.Command("cpio", "-o", "-H", "newc")
	cpioCmd.Dir = tmpDir
	cpioCmd.Stderr = runner.LogWriter()

	gzipCmd := exec.Command("gzip", "-9")
	gzipCmd.Stderr = runner.LogWriter()

	// Pipe: find | cpio | gzip > output
	cpioCmd.Stdin, _ = findCmd.StdoutPipe()
	gzipCmd.Stdin, _ = cpioCmd.StdoutPipe()

	outFile, err := os.Create(output)
	if err != nil {
		return fmt.Errorf("failed to create output file: %w", err)
	}
	defer outFile.Close()
	gzipCmd.Stdout = outFile

	// Start commands in reverse order (gzip -> cpio -> find)
	if err := gzipCmd.Start(); err != nil {
		return fmt.Errorf("failed to start gzip: %w", err)
	}
	if err := cpioCmd.Start(); err != nil {
		gzipCmd.Wait()
		return fmt.Errorf("failed to start cpio: %w", err)
	}
	if err := findCmd.Start(); err != nil {
		cpioCmd.Wait()
		gzipCmd.Wait()
		return fmt.Errorf("failed to start find: %w", err)
	}

	// Wait for all commands to complete
	if err := findCmd.Wait(); err != nil {
		cpioCmd.Wait()
		gzipCmd.Wait()
		return fmt.Errorf("find failed: %w", err)
	}
	if err := cpioCmd.Wait(); err != nil {
		gzipCmd.Wait()
		return fmt.Errorf("cpio failed: %w", err)
	}
	if err := gzipCmd.Wait(); err != nil {
		return fmt.Errorf("gzip failed: %w", err)
	}

	return nil
}
