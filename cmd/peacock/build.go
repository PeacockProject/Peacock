package main

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"time"

	"peacock/internal/builder"
	"peacock/internal/chroot"
	"peacock/internal/config"
	"peacock/internal/image"
	"peacock/internal/manifest"
	"peacock/internal/runner"
	"peacock/pkg/buildconfig"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var deviceName string
var useQemuFlag string
var crossCompileFlag string
var emptyRootfsFlag bool

type buildCleanup struct {
	loopDev     string
	installDir  string
	bootDir     string
	workDir     string
	imageChroot string
}

func (c *buildCleanup) Run() {
	cleanWork := filepath.Clean(c.workDir)
	if c.imageChroot != "" {
		workMount := filepath.Join(c.imageChroot, "work")
		if cleanWork != "" && strings.HasPrefix(filepath.Clean(workMount), cleanWork+string(os.PathSeparator)) {
			_ = chroot.UnmountPathWithSudo(workMount)
		}
		_ = chroot.UnmountWithSudo(c.imageChroot)
	}
	if c.bootDir != "" {
		if cleanWork != "" && strings.HasPrefix(filepath.Clean(c.bootDir), cleanWork+string(os.PathSeparator)) {
			_ = chroot.UnmountPathWithSudo(c.bootDir)
		} else {
			fmt.Fprintf(os.Stderr, "skipping unsafe boot unmount: %s\n", c.bootDir)
		}
	}
	if c.installDir != "" {
		if cleanWork != "" && strings.HasPrefix(filepath.Clean(c.installDir), cleanWork+string(os.PathSeparator)) {
			_ = chroot.UnmountPathWithSudo(c.installDir)
		} else {
			fmt.Fprintf(os.Stderr, "skipping unsafe install unmount: %s\n", c.installDir)
		}
	}
	if c.loopDev != "" {
		_ = image.UnmountLoop(c.loopDev)
	}
}

// buildCmd represents the build command
var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build the distribution image",
	Long: `Build the distribution image for the selected device.
This process involves:
1. Creating a blank image file.
2. Partitioning and formatting.
3. Installing the base system and device packages.
4. Installing the bootloader.`,
	Run: func(cmd *cobra.Command, args []string) {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		runner.SetContext(ctx)

		sigCh := make(chan os.Signal, 1)
		signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
		go func() {
			<-sigCh
			fmt.Println("\nInterrupt received, stopping...")

			// Best-effort cleanup of peacock-owned mountpoints only.
			// This avoids broad mount sweeps that can detach host mounts.
			workDir := config.WorkDir()
			if workDir != "" {
				fmt.Println("Cleaning up mounts...")
				if err := unmountPeacockMounts(workDir); err != nil {
					fmt.Fprintf(os.Stderr, "warning: mount cleanup failed: %v\n", err)
				}
				// Also remove lock files.
				cmd := exec.Command("sudo", "find", workDir, "-name", "db.lck", "-delete")
				cmd.Stdout = os.Stdout
				cmd.Stderr = os.Stderr
				_ = cmd.Run()
			}

			cancel()
		}()

		workDir := config.WorkDir()
		if workDir == "" {
			fmt.Println("Work directory not set. Please run 'peacock init' first.")
			os.Exit(1)
		}
		cleanup := &buildCleanup{workDir: workDir}
		defer cleanup.Run()
		fatal := func() {
			cleanup.Run()
			os.Exit(1)
		}

		logDir := filepath.Join(workDir, "logs")
		if err := os.MkdirAll(logDir, 0755); err == nil {
			logPath := filepath.Join(logDir, fmt.Sprintf("build-%s-%s.log", deviceName, time.Now().Format("20060102-150405")))
			if f, err := os.Create(logPath); err == nil {
				defer f.Close()
				runner.SetLogWriter(f)
				fmt.Printf("Build log: %s\n", logPath)
			}
		}

		// Build the pipeline config from the same flag/viper state the
		// cobra layer has already populated. runBuildSetup's interactive
		// prompts still fire when fields are empty here — non-cobra
		// callers (the Wails GUI) skip those by passing complete cfgs.
		// The outer `cleanup` above stays as the signal-handler safety net;
		// RunBuildPipeline drives its own internal cleanup in lockstep with
		// phase 4 so loop devs / image-build chroot mounts get released.
		cfg := buildconfig.BuildPipelineConfig{
			Device:         deviceName,
			Flavor:         config.Flavor(),
			InitSystem:     config.InitSystem(),
			Desktop:        config.Desktop(),
			DisplayManager: config.DisplayManager(),
			Extras:         config.ExtraPackages(),
			UserName:       config.UserName(),
			UserPassword:   config.UserPassword(),
			ImageSizeMB:    config.ImageSizeMB(),
			EmptyRootfs:    config.EmptyRootfs(),
			UseQemu:        useQemuFlag,
			CrossCompile:   crossCompileFlag,
			WorkDir:        workDir,
		}

		imagePath, err := RunBuildPipeline(ctx, cfg)
		if err != nil {
			fmt.Printf("%v\n", err)
			fatal()
		}

		// 10. Build complete
		fmt.Println("Build complete! Image at: " + imagePath)
	},
}

func execCommand(name string, arg ...string) error {
	return runner.Run(name, arg...)
}

func unmountPeacockMounts(workDir string) error {
	roots := []string{
		filepath.Join(workDir, "build-chroot"),
		filepath.Join(workDir, "image-build-chroot"),
	}
	mounts, err := mountPointsUnder(roots)
	if err != nil {
		return err
	}
	for _, mp := range mounts {
		if err := chroot.UnmountPathWithSudo(mp); err != nil {
			fmt.Fprintf(os.Stderr, "warning: failed to unmount %s: %v\n", mp, err)
		}
	}
	return nil
}

func unmountRootfsSubmounts(rootfsPath string) error {
	if rootfsPath == "" {
		return nil
	}
	mounts, err := mountPointsUnder([]string{rootfsPath})
	if err != nil {
		return err
	}
	for _, mp := range mounts {
		if err := chroot.UnmountPathWithSudo(mp); err != nil {
			fmt.Fprintf(os.Stderr, "warning: failed to unmount %s: %v\n", mp, err)
		}
	}
	return nil
}

func mountPointsUnder(roots []string) ([]string, error) {
	data, err := os.ReadFile("/proc/self/mountinfo")
	if err != nil {
		return nil, err
	}

	normalizedRoots := make([]string, 0, len(roots))
	for _, root := range roots {
		cleanRoot := filepath.Clean(root)
		if cleanRoot == "" || cleanRoot == "." || cleanRoot == "/" {
			continue
		}
		normalizedRoots = append(normalizedRoots, cleanRoot)
	}

	set := make(map[string]struct{})
	lines := strings.Split(string(data), "\n")
	for _, line := range lines {
		if line == "" {
			continue
		}
		fields := strings.Fields(line)
		if len(fields) < 5 {
			continue
		}

		mp := filepath.Clean(decodeMountInfoPath(fields[4]))
		for _, root := range normalizedRoots {
			if mp == root || strings.HasPrefix(mp, root+string(os.PathSeparator)) {
				set[mp] = struct{}{}
				break
			}
		}
	}

	out := make([]string, 0, len(set))
	for mp := range set {
		out = append(out, mp)
	}
	sort.Slice(out, func(i, j int) bool {
		if len(out[i]) == len(out[j]) {
			return out[i] > out[j]
		}
		return len(out[i]) > len(out[j])
	})
	return out, nil
}

func decodeMountInfoPath(path string) string {
	var b strings.Builder
	b.Grow(len(path))
	for i := 0; i < len(path); i++ {
		if path[i] == '\\' && i+3 < len(path) &&
			path[i+1] >= '0' && path[i+1] <= '7' &&
			path[i+2] >= '0' && path[i+2] <= '7' &&
			path[i+3] >= '0' && path[i+3] <= '7' {
			val := (path[i+1]-'0')*64 + (path[i+2]-'0')*8 + (path[i+3] - '0')
			b.WriteByte(val)
			i += 3
			continue
		}
		b.WriteByte(path[i])
	}
	return b.String()
}

func kernelArtifactExists(buildDir string) bool {
	if buildDir == "" {
		return false
	}
	candidates := []string{
		filepath.Join(buildDir, "zImage"),
		filepath.Join(buildDir, "Image.gz"),
		filepath.Join(buildDir, "Image"),
		filepath.Join(buildDir, "arch", "arm", "boot", "zImage"),
		filepath.Join(buildDir, "arch", "arm64", "boot", "Image.gz"),
		filepath.Join(buildDir, "arch", "arm64", "boot", "Image"),
	}
	for _, p := range candidates {
		if fileExistsFile(p) {
			return true
		}
	}
	return false
}

func dtbPreferenceTokens(deviceName string) []string {
	deviceName = strings.ToLower(strings.TrimSpace(deviceName))
	if deviceName == "" {
		return nil
	}
	raw := strings.FieldsFunc(deviceName, func(r rune) bool {
		return (r < 'a' || r > 'z') && (r < '0' || r > '9')
	})
	seen := map[string]bool{}
	out := make([]string, 0, len(raw)+1)
	for _, t := range raw {
		if t == "" || seen[t] {
			continue
		}
		seen[t] = true
		out = append(out, t)
	}
	if !seen[deviceName] {
		out = append(out, deviceName)
	}
	return out
}

func discoverKernelDTB(kernelBuildDir, deviceName string) string {
	if kernelBuildDir == "" {
		return ""
	}

	roots := []string{
		filepath.Join(kernelBuildDir, "dtbs"),
		filepath.Join(kernelBuildDir, "arch", "arm", "boot", "dts"),
		filepath.Join(kernelBuildDir, "arch", "arm64", "boot", "dts"),
	}

	first := ""
	best := ""
	bestScore := 0
	tokens := dtbPreferenceTokens(deviceName)
	for _, root := range roots {
		if !fileExists(root) {
			continue
		}
		_ = filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
			if err != nil || info == nil || info.IsDir() {
				return nil
			}
			if !strings.HasSuffix(strings.ToLower(info.Name()), ".dtb") {
				return nil
			}
			if first == "" {
				first = path
			}
			if len(tokens) == 0 {
				return nil
			}
			lowerName := strings.ToLower(info.Name())
			score := 0
			for _, token := range tokens {
				if strings.Contains(lowerName, token) {
					score++
				}
			}
			if score > bestScore {
				bestScore = score
				best = path
			}
			return nil
		})
	}
	if best != "" {
		return best
	}
	return first
}

func stageExtlinuxBootAssets(rootfsPath, kernelPath, initramfsPath, cmdline, dtbPath string) error {
	bootDir := filepath.Join(rootfsPath, "boot")
	extlinuxDir := filepath.Join(bootDir, "extlinux")
	if err := execCommand("sudo", "mkdir", "-p", extlinuxDir); err != nil {
		return err
	}

	if err := execCommand("sudo", "install", "-m", "0644", kernelPath, filepath.Join(bootDir, "zImage")); err != nil {
		return err
	}
	// Keep a generic kernel filename for easier manual debugging.
	_ = execCommand("sudo", "install", "-m", "0644", kernelPath, filepath.Join(bootDir, "vmlinuz"))

	if err := execCommand("sudo", "install", "-m", "0644", initramfsPath, filepath.Join(bootDir, "initramfs.cpio.gz")); err != nil {
		return err
	}
	// Compatibility alias used by existing helper scripts.
	_ = execCommand("sudo", "install", "-m", "0644", initramfsPath, filepath.Join(bootDir, "initramfs-linux.img"))

	dtbLine := ""
	if dtbPath != "" && fileExistsFile(dtbPath) {
		dtbDir := filepath.Join(bootDir, "dtbs")
		if err := execCommand("sudo", "mkdir", "-p", dtbDir); err != nil {
			return err
		}
		dtbName := filepath.Base(dtbPath)
		if err := execCommand("sudo", "install", "-m", "0644", dtbPath, filepath.Join(dtbDir, dtbName)); err != nil {
			return err
		}
		dtbLine = fmt.Sprintf("    fdt /dtbs/%s\n", dtbName)
	}

	normalizedCmdline := strings.TrimSpace(strings.ReplaceAll(cmdline, "\n", " "))
	conf := fmt.Sprintf(`default peacock
label peacock
    linux /zImage
    initrd /initramfs.cpio.gz
%s    append %s
`, dtbLine, normalizedCmdline)

	tmp, err := os.CreateTemp("", "peacock-extlinux-*.conf")
	if err != nil {
		return err
	}
	tmpPath := tmp.Name()
	if _, err := tmp.WriteString(conf); err != nil {
		_ = tmp.Close()
		_ = os.Remove(tmpPath)
		return err
	}
	if err := tmp.Close(); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}
	defer os.Remove(tmpPath)

	return execCommand("sudo", "install", "-m", "0644", tmpPath, filepath.Join(extlinuxDir, "extlinux.conf"))
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && info.IsDir()
}

func fileExistsFile(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

func pacmanArch(arch string) string {
	if arch == "armv7" {
		return "armv7h"
	}
	return arch
}

func cachedArtifactPath(cacheDir, name, version, arch string) string {
	pacArch := pacmanArch(arch)
	candidates := []string{
		filepath.Join(cacheDir, fmt.Sprintf("%s-%s-1-%s.pkg.tar.gz", name, version, pacArch)),
		filepath.Join(cacheDir, fmt.Sprintf("%s-%s-1-%s.pkg.tar.gz", name, version, arch)),
	}
	for _, path := range candidates {
		if _, err := os.Stat(path); err == nil {
			return path
		}
	}
	// Migrate legacy filename without pkgrel if present.
	legacyCandidates := []string{
		filepath.Join(cacheDir, fmt.Sprintf("%s-%s-%s.pkg.tar.gz", name, version, pacArch)),
		filepath.Join(cacheDir, fmt.Sprintf("%s-%s-%s.pkg.tar.gz", name, version, arch)),
	}
	for _, legacy := range legacyCandidates {
		if _, err := os.Stat(legacy); err == nil {
			target := filepath.Join(cacheDir, fmt.Sprintf("%s-%s-1-%s.pkg.tar.gz", name, version, pacArch))
			if err := os.Rename(legacy, target); err == nil {
				return target
			}
			return legacy
		}
	}
	return ""
}

func extractKernelFromPackage(pkgPath, workDir string) (string, error) {
	dest := filepath.Join(workDir, "kernel-cache", filepath.Base(pkgPath))
	if err := os.MkdirAll(dest, 0755); err != nil {
		return "", err
	}
	zImagePath := filepath.Join(dest, "zImage")
	if fileExistsFile(zImagePath) {
		return dest, nil
	}
	cmd := exec.Command("tar", "-xzf", pkgPath, "-C", dest, "zImage", "modules.tar.gz")
	if err := runner.RunCmd(cmd); err != nil {
		return "", fmt.Errorf("failed to extract zImage from %s: %w", pkgPath, err)
	}
	if !fileExistsFile(zImagePath) {
		return "", fmt.Errorf("zImage not found in %s", pkgPath)
	}
	return dest, nil
}

func extractBusyboxFromPackage(pkgPath, workDir string) (string, error) {
	dest := filepath.Join(workDir, "busybox-cache", filepath.Base(pkgPath))
	if err := os.MkdirAll(dest, 0755); err != nil {
		return "", err
	}
	busyboxPath := filepath.Join(dest, "busybox")
	if fileExistsFile(busyboxPath) {
		return dest, nil
	}
	cmd := exec.Command("tar", "-xzf", pkgPath, "-C", dest, "busybox")
	if err := runner.RunCmd(cmd); err != nil {
		return "", fmt.Errorf("failed to extract busybox from %s: %w", pkgPath, err)
	}
	if !fileExistsFile(busyboxPath) {
		return "", fmt.Errorf("busybox not found in %s", pkgPath)
	}
	return dest, nil
}

func extractRefresherFromPackage(pkgPath, workDir string) (string, error) {
	dest := filepath.Join(workDir, "refresher-cache", filepath.Base(pkgPath))
	if err := os.MkdirAll(dest, 0755); err != nil {
		return "", err
	}
	refresherPath := filepath.Join(dest, "usr", "bin", "msm-fb-refresher")
	if fileExistsFile(refresherPath) {
		return dest, nil
	}
	cmd := exec.Command("tar", "-xzf", pkgPath, "-C", dest, "usr/bin/msm-fb-refresher")
	if err := runner.RunCmd(cmd); err != nil {
		return "", fmt.Errorf("failed to extract msm-fb-refresher from %s: %w", pkgPath, err)
	}
	if !fileExistsFile(refresherPath) {
		return "", fmt.Errorf("msm-fb-refresher not found in %s", pkgPath)
	}
	return dest, nil
}

func packageArchMatches(pkgPath, expectedArch string) bool {
	f, err := os.Open(pkgPath)
	if err != nil {
		return false
	}
	defer f.Close()

	gzr, err := gzip.NewReader(f)
	if err != nil {
		return false
	}
	defer gzr.Close()

	tr := tar.NewReader(gzr)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			return false
		}
		if err != nil {
			return false
		}
		if hdr.Name != ".PKGINFO" {
			continue
		}
		data, err := io.ReadAll(tr)
		if err != nil {
			return false
		}
		foundArch := ""
		hasRel := false
		pkgVer := ""
		for _, line := range strings.Split(string(data), "\n") {
			if strings.HasPrefix(line, "arch = ") {
				foundArch = strings.TrimSpace(strings.TrimPrefix(line, "arch = "))
			}
			if strings.HasPrefix(line, "pkgrel = ") {
				hasRel = true
			}
			if strings.HasPrefix(line, "pkgver = ") {
				pkgVer = strings.TrimSpace(strings.TrimPrefix(line, "pkgver = "))
			}
		}
		return foundArch == expectedArch && !hasRel && strings.HasSuffix(pkgVer, "-1")
	}
}

func isMounted(path string) bool {
	cmd := exec.Command("mountpoint", "-q", path)
	return runner.RunCmd(cmd) == nil
}

func findPaths(root string, names map[string]struct{}) ([]string, error) {
	var found []string
	err := filepath.WalkDir(root, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			if _, ok := names[filepath.Base(path)]; ok {
				found = append(found, path)
				return filepath.SkipDir
			}
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return found, nil
}

type preparedBuildDeps struct {
	Bin []string
	Inc []string
	Lib []string
	LD  []string
}

func boolPtr(v bool) *bool {
	return &v
}

// localPackageManifestPath searches peacock-ports/{device,base}/<name>/package.toml
// and returns the first hit. Used to decide whether a dependency is locally built
// or fetched from a remote pacman repo.
func localPackageManifestPath(name string) (string, bool) {
	candidates := []string{
		filepath.Join("peacock-ports", "device", name, "package.toml"),
		filepath.Join("peacock-ports", "base", name, "package.toml"),
	}
	for _, c := range candidates {
		if _, err := os.Stat(c); err == nil {
			return c, true
		}
	}
	return "", false
}

// findCachedPackageArtifact returns a path to a cached .pkg.tar.gz for pkg+arch
// when one exists and its embedded arch matches the expected pacman arch.
// Returns "" when no usable cached artifact is found. Mismatched cache entries
// are reported to stdout so the caller can rebuild transparently.
func findCachedPackageArtifact(b *builder.Builder, pkg *manifest.Package, targetArch string) string {
	artifactPath := cachedArtifactPath(b.CacheDir, pkg.Package.Name, pkg.Package.Version, targetArch)
	if artifactPath == "" {
		return ""
	}
	if !packageArchMatches(artifactPath, pacmanArch(targetArch)) {
		fmt.Printf("Cached package %s has mismatched arch; rebuilding\n", artifactPath)
		return ""
	}
	return artifactPath
}

// locatePeacockMkinitfs returns the absolute path to the peacock-mkinitfs
// binary, preferring (in order):
//
//  1. <portBuildDir>/usr/bin/peacock-mkinitfs    — fresh build from the port.
//  2. <portBuildDir>/stage/usr/bin/peacock-mkinitfs — staged install layout.
//  3. <portBuildDir>/peacock-mkinitfs            — top-level Makefile output.
//  4. exec.LookPath("peacock-mkinitfs")          — system install / dev shell.
//
// Returns "" when nothing resolves; the caller should treat that as fatal.
func locatePeacockMkinitfs(portBuildDir string) string {
	if portBuildDir != "" {
		candidates := []string{
			filepath.Join(portBuildDir, "usr", "bin", "peacock-mkinitfs"),
			filepath.Join(portBuildDir, "stage", "usr", "bin", "peacock-mkinitfs"),
			filepath.Join(portBuildDir, "peacock-mkinitfs"),
		}
		for _, c := range candidates {
			if fileExistsFile(c) {
				return c
			}
		}
	}
	if p, err := exec.LookPath("peacock-mkinitfs"); err == nil {
		return p
	}
	return ""
}

// buildPortForInitramfs loads a port package from peacock-ports/base/<name>
// and produces (or reuses a cached) build directory containing its staged
// payload (sbin/, lib/, etc.). Used to source util-linux + lvm2 binaries for
// the initramfs after the PRP vendor tree was dropped.
//
// Returns the build-dir path on success, "" on any error (errors are logged
// to stdout). Callers MUST tolerate "" gracefully — the initramfs builder
// falls back to host paths when the supplied dir is empty.
func buildPortForInitramfs(b *builder.Builder, name, targetArch, workDir, useQemuFlag, crossCompileFlag string) string {
	manifestPath := filepath.Join("peacock-ports", "base", name, "package.toml")
	pkg, err := manifest.LoadPackage(manifestPath)
	if err != nil {
		fmt.Printf("Warning: skipping %s for initramfs (manifest load failed): %v\n", name, err)
		return ""
	}

	// Compute the build-dir path so we can reuse a previous in-chroot build
	// without re-running the full pipeline when only the .pkg.tar.gz is cached.
	_, chrootArch, err := resolveBuildOptions(pkg, targetArch, useQemuFlag, crossCompileFlag)
	if err != nil {
		fmt.Printf("Warning: skipping %s for initramfs (resolveBuildOptions failed): %v\n", name, err)
		return ""
	}
	buildChrootDir := filepath.Join(workDir, "build-chroot", chrootArch)
	buildDirHint := filepath.Join(buildChrootDir, "build", fmt.Sprintf("%s-%s-%s", pkg.Package.Name, pkg.Package.Version, targetArch))

	if artifactPath := findCachedPackageArtifact(b, pkg, targetArch); artifactPath != "" {
		if fileExists(buildDirHint) {
			fmt.Printf("Reusing cached %s build dir at %s\n", name, buildDirHint)
			return buildDirHint
		}
		fmt.Printf("Cached %s package present but build dir missing; rebuilding for initramfs\n", name)
	}

	fmt.Printf("Building %s for initramfs...\n", name)
	buildDir, _, err := buildPackageInChrootStep(b, pkg, targetArch, workDir, useQemuFlag, crossCompileFlag)
	if err != nil {
		fmt.Printf("Warning: skipping %s for initramfs (build failed): %v\n", name, err)
		return ""
	}
	return buildDir
}

// buildPackageInChrootStep performs the full chroot-build pipeline for a single
// package: resolve build options, ensure+bootstrap a build chroot for the right
// arch, stage build_dep_packages into it, run the build, and emit the final
// .pkg.tar.gz. It does NOT consult the artifact cache — callers that want to
// skip rebuilds should check findCachedPackageArtifact first. Returns the
// in-chroot build directory and the produced artifact path.
func buildPackageInChrootStep(b *builder.Builder, pkg *manifest.Package, targetArch, workDir, useQemuFlag, crossCompileFlag string) (string, string, error) {
	opts, chrootArch, err := resolveBuildOptions(pkg, targetArch, useQemuFlag, crossCompileFlag)
	if err != nil {
		return "", "", fmt.Errorf("resolving build options: %w", err)
	}
	buildChrootDir := filepath.Join(workDir, "build-chroot", chrootArch)
	buildDepChrootRoot := filepath.Join(workDir, "build-dep-chroot", hostArchString())
	useQemu := opts.UseQemu != nil && *opts.UseQemu

	if err := b.EnsureBuildChroot(buildChrootDir, chrootArch, useQemu); err != nil {
		return "", "", fmt.Errorf("ensuring build chroot: %w", err)
	}
	if err := ensureBuildChrootBootstrap(b, buildChrootDir, chrootArch); err != nil {
		return "", "", fmt.Errorf("bootstrapping build chroot: %w", err)
	}
	extraPaths, err := prepareBuildDepPackages(b, pkg, buildChrootDir, buildDepChrootRoot)
	if err != nil {
		return "", "", fmt.Errorf("preparing build_dep_packages: %w", err)
	}
	opts.ExtraPath = extraPaths.Bin
	opts.ExtraInclude = extraPaths.Inc
	opts.ExtraLib = extraPaths.Lib
	opts.ExtraLdLib = extraPaths.LD

	buildDir, err := b.BuildPackageInChroot(pkg, targetArch, buildChrootDir, opts)
	if err != nil {
		return "", "", fmt.Errorf("building package: %w", err)
	}
	artifactPath, err := b.PackageArtifact(buildDir, pkg, targetArch)
	if err != nil {
		return buildDir, "", fmt.Errorf("packaging artifact: %w", err)
	}
	return buildDir, artifactPath, nil
}

func prepareBuildDepPackages(b *builder.Builder, pkg *manifest.Package, chrootRoot string, buildDepChrootRoot string) (preparedBuildDeps, error) {
	if len(pkg.Build.BuildDepPackages) == 0 {
		return preparedBuildDeps{}, nil
	}

	if err := ensureBuildDepBootstrap(b, buildDepChrootRoot); err != nil {
		return preparedBuildDeps{}, err
	}

	var extra preparedBuildDeps
	for _, depPkg := range pkg.Build.BuildDepPackages {
		candidates := []string{
			filepath.Join("peacock-ports", "base", depPkg, "package.toml"),
			filepath.Join("peacock-ports", "device", depPkg, "package.toml"),
		}
		var found string
		for _, c := range candidates {
			if _, err := os.Stat(c); err == nil {
				found = c
				break
			}
		}
		if found == "" {
			return preparedBuildDeps{}, fmt.Errorf("build_dep_package %s not found in peacock-ports/base or peacock-ports/device", depPkg)
		}

		depManifest, err := manifest.LoadPackage(found)
		if err != nil {
			return preparedBuildDeps{}, fmt.Errorf("failed to load build_dep_package %s: %w", depPkg, err)
		}

		hostArch := hostArchString()
		artifactPath := cachedArtifactPath(b.CacheDir, depManifest.Package.Name, depManifest.Package.Version, hostArch)
		if artifactPath == "" {
			buildDir, err := b.BuildPackageInChroot(depManifest, hostArch, buildDepChrootRoot, builder.BuildOptions{
				UseQemu: boolPtr(false),
			})
			if err != nil {
				return preparedBuildDeps{}, fmt.Errorf("failed to build build_dep_package %s in chroot: %w", depPkg, err)
			}

			artifactPath, err = b.PackageArtifact(buildDir, depManifest, hostArch)
			if err != nil {
				return preparedBuildDeps{}, fmt.Errorf("failed to package build_dep_package %s: %w", depPkg, err)
			}
		}

		destRoot := filepath.Join(chrootRoot, "usr", "local")
		if err := execCommand("sudo", "mkdir", "-p", destRoot); err != nil {
			return preparedBuildDeps{}, fmt.Errorf("failed to create build_dep_package dest %s: %w", destRoot, err)
		}

		if err := execCommand("sudo", "tar", "-xzf", artifactPath, "-C", destRoot); err != nil {
			return preparedBuildDeps{}, fmt.Errorf("failed to install build_dep_package %s: %w", depPkg, err)
		}

		bins, err := findPaths(destRoot, map[string]struct{}{"bin": {}})
		if err != nil {
			return preparedBuildDeps{}, fmt.Errorf("failed to detect bin paths for %s: %w", depPkg, err)
		}
		incs, err := findPaths(destRoot, map[string]struct{}{"include": {}})
		if err != nil {
			return preparedBuildDeps{}, fmt.Errorf("failed to detect include paths for %s: %w", depPkg, err)
		}
		libs, err := findPaths(destRoot, map[string]struct{}{"lib": {}, "lib64": {}})
		if err != nil {
			return preparedBuildDeps{}, fmt.Errorf("failed to detect lib paths for %s: %w", depPkg, err)
		}
		for _, p := range bins {
			rel, err := filepath.Rel(chrootRoot, p)
			if err != nil {
				return preparedBuildDeps{}, fmt.Errorf("failed to relativize bin path %s: %w", p, err)
			}
			extra.Bin = append(extra.Bin, filepath.Join(string(os.PathSeparator), rel))
		}
		for _, p := range incs {
			rel, err := filepath.Rel(chrootRoot, p)
			if err != nil {
				return preparedBuildDeps{}, fmt.Errorf("failed to relativize include path %s: %w", p, err)
			}
			extra.Inc = append(extra.Inc, filepath.Join(string(os.PathSeparator), rel))
		}
		for _, p := range libs {
			rel, err := filepath.Rel(chrootRoot, p)
			if err != nil {
				return preparedBuildDeps{}, fmt.Errorf("failed to relativize lib path %s: %w", p, err)
			}
			extra.Lib = append(extra.Lib, filepath.Join(string(os.PathSeparator), rel))
			extra.LD = append(extra.LD, filepath.Join(string(os.PathSeparator), rel))
		}
	}

	return extra, nil
}

func ensureBuildDepBootstrap(b *builder.Builder, root string) error {
	hostArch := hostArchString()
	if err := b.EnsureBuildChroot(root, hostArch, false); err != nil {
		return fmt.Errorf("failed to ensure build-dep chroot: %w", err)
	}
	if _, err := os.Stat(filepath.Join(root, "usr", "bin", "make")); err == nil {
		if _, err := os.Stat(filepath.Join(root, "usr", "bin", "gcc")); err == nil {
			return nil
		}
	}
	return bootstrapBaseDevel(root)
}

func ensureBuildChrootBootstrap(b *builder.Builder, root string, chrootArch string) error {
	if chrootArch != hostArchString() {
		return nil
	}
	if err := b.EnsureBuildChroot(root, chrootArch, false); err != nil {
		return fmt.Errorf("failed to ensure build chroot: %w", err)
	}
	if _, err := os.Stat(filepath.Join(root, "usr", "bin", "gcc")); err == nil {
		if _, err := os.Stat(filepath.Join(root, "usr", "bin", "python")); err == nil {
			return nil
		}
	}
	return bootstrapBaseDevel(root)
}

func bootstrapBaseDevel(root string) error {
	return bootstrapPacmanPackages(root, []string{"base-devel", "python"})
}

func bootstrapPacmanPackages(root string, packages []string) error {
	confPath := filepath.Join(root, "etc", "pacman.conf")
	confData, err := os.ReadFile(confPath)
	if err != nil {
		return fmt.Errorf("failed to read pacman.conf: %w", err)
	}
	var confLines []string
	for _, line := range strings.Split(string(confData), "\n") {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "DownloadUser") {
			confLines = append(confLines, "#"+line)
			continue
		}
		confLines = append(confLines, line)
	}
	tmpConf := filepath.Join(os.TempDir(), fmt.Sprintf("peacock-pacman-%d.conf", time.Now().UnixNano()))
	if err := os.WriteFile(tmpConf, []byte(strings.Join(confLines, "\n")), 0644); err != nil {
		return fmt.Errorf("failed to write pacman bootstrap config: %w", err)
	}
	defer os.Remove(tmpConf)

	if err := execCommand("sudo", "mkdir", "-p", filepath.Join(root, "var", "cache", "pacman", "pkg")); err != nil {
		return fmt.Errorf("failed to create pacman cache dir: %w", err)
	}
	_ = execCommand("sudo", "chown", "-R", "root:root", filepath.Join(root, "var", "lib", "pacman"))
	syncDir := filepath.Join(root, "var", "lib", "pacman", "sync")
	_ = execCommand("sudo", "sh", "-c", fmt.Sprintf("rm -rf %q/download-*", syncDir))
	_ = execCommand("sudo", "pacman-key", "--gpgdir", filepath.Join(root, "etc", "pacman.d", "gnupg"), "--init")
	_ = execCommand("sudo", "pacman-key", "--gpgdir", filepath.Join(root, "etc", "pacman.d", "gnupg"), "--populate", "archlinux")
	args := []string{
		"pacman",
		"-Sy",
		"--noconfirm",
		"--root", root,
		"--dbpath", filepath.Join(root, "var", "lib", "pacman"),
		"--cachedir", filepath.Join(root, "var", "cache", "pacman", "pkg"),
		"--cachedir", "/var/cache/pacman/pkg",
		"--config", tmpConf,
	}
	args = append(args, packages...)
	if err := execCommand("sudo", args...); err != nil {
		return fmt.Errorf("failed to bootstrap packages in chroot: %w", err)
	}
	_ = execCommand("sudo", "rm", "-rf", filepath.Join(root, "var", "cache", "pacman", "pkg", "*"))
	return nil
}

func ensureImageBuildChroot(b *builder.Builder, workDir string) (string, error) {
	root := filepath.Join(workDir, "image-build-chroot", hostArchString())
	if err := b.EnsureBuildChroot(root, hostArchString(), false); err != nil {
		return "", err
	}
	if err := chroot.MountWithSudo(root); err != nil {
		return "", err
	}
	workMount := filepath.Join(root, "work")
	if err := execCommand("sudo", "mkdir", "-p", workMount); err != nil {
		return "", err
	}
	if !isMounted(workMount) {
		if err := execCommand("sudo", "mount", "--bind", workDir, workMount); err != nil {
			return "", err
		}
	}
	if err := bootstrapPacmanPackages(root, []string{"base-devel", "python", "util-linux", "e2fsprogs", "dosfstools", "qemu-user-static"}); err != nil {
		return "", err
	}
	return root, nil
}

func hostArchString() string {
	switch runtime.GOARCH {
	case "amd64":
		return "x86_64"
	case "arm64":
		return "aarch64"
	case "arm":
		return "armv7"
	default:
		return runtime.GOARCH
	}
}

func parseUseQemu(flag string) (*bool, error) {
	switch flag {
	case "auto", "":
		return nil, nil
	case "true":
		val := true
		return &val, nil
	case "false":
		val := false
		return &val, nil
	default:
		return nil, fmt.Errorf("invalid --use-qemu value: %s (expected auto|true|false)", flag)
	}
}

func resolveBuildOptions(pkg *manifest.Package, targetArch string, useQemuFlag string, crossCompileFlag string) (builder.BuildOptions, string, error) {
	flagUseQemu, err := parseUseQemu(useQemuFlag)
	if err != nil {
		return builder.BuildOptions{}, "", err
	}

	crossCompile := crossCompileFlag
	if crossCompile == "" {
		crossCompile = pkg.Build.CrossCompile
	}

	useQemu := flagUseQemu
	if useQemu == nil {
		useQemu = pkg.Build.UseQemu
	}

	if useQemu == nil {
		if crossCompile != "" {
			val := false
			useQemu = &val
		} else if targetArch != hostArchString() {
			val := true
			useQemu = &val
		} else {
			val := false
			useQemu = &val
		}
	}

	chrootArch := targetArch
	if useQemu != nil && !*useQemu {
		chrootArch = hostArchString()
	}

	return builder.BuildOptions{
		UseQemu:      useQemu,
		CrossCompile: crossCompile,
		Flavor:       config.Flavor(),
	}, chrootArch, nil
}

func init() {
	rootCmd.AddCommand(buildCmd)
	buildCmd.Flags().StringVar(&deviceName, "device", "", "Device codename (e.g. samsung-i9500)")
	buildCmd.Flags().String("init", "systemd", "Init system (systemd, openrc)")
	buildCmd.Flags().String("desktop", "", "Desktop environment (none, xfce, lxqt, mate, gnome, plasma, cinnamon)")
	buildCmd.Flags().String("display-manager", "", "Display manager (none, lightdm, greetd, sddm, gdm, ly)")
	buildCmd.Flags().StringSlice("extra", nil, "Extra packages to include in rootfs")
	buildCmd.Flags().String("user", "", "Create user account in rootfs")
	buildCmd.Flags().String("password", "", "Password for --user (plaintext)")
	buildCmd.Flags().Int("image-size", 0, "Disk image size in MB (0 = auto)")
	buildCmd.Flags().BoolVar(&emptyRootfsFlag, "empty-rootfs", false, "Create a small debug image with boot assets only and an empty labeled root partition")
	buildCmd.Flags().StringVar(&useQemuFlag, "use-qemu", "auto", "Use qemu for foreign arch builds: auto|true|false")
	buildCmd.Flags().StringVar(&crossCompileFlag, "cross-compile", "", "Cross compiler prefix (e.g. arm-none-eabi-)")
	buildCmd.Flags().String("flavor", "arch", "Base-distro flavor: arch|debian|alpine")
	viper.BindPFlag(config.KeyFlavor, buildCmd.Flags().Lookup("flavor"))
	viper.BindPFlag(config.KeyInitSystem, buildCmd.Flags().Lookup("init"))
	viper.BindPFlag(config.KeyDesktop, buildCmd.Flags().Lookup("desktop"))
	viper.BindPFlag(config.KeyDisplayManager, buildCmd.Flags().Lookup("display-manager"))
	viper.BindPFlag(config.KeyExtraPackages, buildCmd.Flags().Lookup("extra"))
	viper.BindPFlag(config.KeyUserName, buildCmd.Flags().Lookup("user"))
	viper.BindPFlag(config.KeyUserPassword, buildCmd.Flags().Lookup("password"))
	viper.BindPFlag(config.KeyImageSizeMB, buildCmd.Flags().Lookup("image-size"))
	viper.BindPFlag(config.KeyEmptyRootfs, buildCmd.Flags().Lookup("empty-rootfs"))
	buildCmd.MarkFlagRequired("device")
}

func estimateImageSizeMB(rootfsPath string, emptyRootfs bool) int {
	if emptyRootfs {
		const minMB = 768
		const overheadMB = 64

		usedMB := duSizeMB(rootfsPath)
		size := usedMB + overheadMB
		if size < minMB {
			size = minMB
		}
		// Round up to nearest 64MB for compact debug images.
		size = ((size + 63) / 64) * 64
		return size
	}

	const minMB = 2048
	const overheadMB = 256

	usedMB := duSizeMB(rootfsPath)
	if usedMB <= 0 {
		return minMB
	}
	// 30% headroom + fixed overhead
	size := (usedMB*13 + 9) / 10
	size += overheadMB
	if size < minMB {
		size = minMB
	}
	// Round up to nearest 256MB
	size = ((size + 255) / 256) * 256
	return size
}

func duSizeMB(path string) int {
	out, err := exec.Command("sudo", "du", "-s", "-B1M", path).Output()
	if err != nil {
		out, err = exec.Command("sudo", "du", "-m", "-s", path).Output()
		if err != nil {
			return 0
		}
	}
	fields := strings.Fields(string(out))
	if len(fields) == 0 {
		return 0
	}
	val, err := strconv.Atoi(fields[0])
	if err != nil {
		return 0
	}
	return val
}

func promptLine(r *bufio.Reader, label, def string) string {
	if def != "" {
		fmt.Printf("%s [%s]: ", label, def)
	} else {
		fmt.Printf("%s: ", label)
	}
	line, _ := r.ReadString('\n')
	line = strings.TrimSpace(line)
	if line == "" {
		return def
	}
	return line
}

func promptCSV(r *bufio.Reader, label string) []string {
	line := promptLine(r, label, "")
	if line == "" {
		return nil
	}
	parts := strings.Split(line, ",")
	out := make([]string, 0, len(parts))
	for _, p := range parts {
		p = strings.TrimSpace(p)
		if p != "" {
			out = append(out, p)
		}
	}
	return out
}

func promptSelect(r *bufio.Reader, label string, options []string, def string) string {
	fmt.Printf("%s options: %s\n", label, strings.Join(options, ", "))
	for {
		v := promptLine(r, label, def)
		for _, o := range options {
			if v == o {
				return v
			}
		}
		fmt.Printf("Invalid %s: %s\n", label, v)
	}
}

func promptPassword(r *bufio.Reader, label, confirmLabel string) string {
	for {
		pw := promptLine(r, label, "")
		confirm := promptLine(r, confirmLabel, "")
		if pw == confirm {
			return pw
		}
		fmt.Println("Passwords do not match, try again.")
	}
}
