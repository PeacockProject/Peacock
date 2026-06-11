package pipeline

// Lifted from cmd/peacock/build.go. These are the cross-phase helpers
// the build pipeline (and the standalone build-packages / bisect
// subcommands on the cobra side) consume. Exported names — UpperCamel —
// are the ones cmd/peacock still needs to reach directly; the rest stay
// package-internal.

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"peacock/internal/builder"
	"peacock/internal/chroot"
	"peacock/internal/config"
	"peacock/internal/manifest"
	"peacock/internal/runner"
)

// execCommand wraps runner.Run so the rest of the helpers don't have to
// thread the runner package through their parameter lists.
func execCommand(name string, arg ...string) error {
	return runner.Run(name, arg...)
}

// UnmountPeacockMounts walks /proc/self/mountinfo and unmounts every
// mountpoint nested under the well-known peacock chroot dirs. The
// cobra signal handler calls this directly so a Ctrl-C teardown
// doesn't leave dev / proc / sys mounts dangling under build-chroot/
// or image-build-chroot/.
func UnmountPeacockMounts(workDir string) error {
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

// packagesStoreDir is the per-arch built-package store, a sibling of the
// cache dir (mirrors builder.Builder.PackagesDir).
func packagesStoreDir(cacheDir string) string {
	return filepath.Join(filepath.Dir(cacheDir), "packages")
}

func cachedArtifactPath(cacheDir, name, version, arch string) string {
	pacArch := pacmanArch(arch)
	archDir := filepath.Join(packagesStoreDir(cacheDir), pacArch)
	// .feather packages in the per-arch store (packages/<arch>/...),
	// arch-name alt dir for back-compat with raw arch naming.
	candidates := []string{
		filepath.Join(archDir, fmt.Sprintf("%s-%s-1-%s.feather", name, version, pacArch)),
		filepath.Join(packagesStoreDir(cacheDir), arch, fmt.Sprintf("%s-%s-1-%s.feather", name, version, arch)),
	}
	for _, path := range candidates {
		if _, err := os.Stat(path); err == nil {
			return path
		}
	}
	// (Legacy pacman .pkg.tar.gz artifacts are a different format and are
	// not reused — they get rebuilt as .feather on demand.)
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

// LocalPackageManifestPath searches peacock-ports/{device,base}/<name>/package.toml
// and returns the first hit. Used to decide whether a dependency is locally built
// or fetched from a remote pacman repo.
func LocalPackageManifestPath(name string) (string, bool) {
	candidates := []string{
		filepath.Join(portsRoot, "device", name, "package.toml"),
		filepath.Join(portsRoot, "base", name, "package.toml"),
	}
	for _, c := range candidates {
		if _, err := os.Stat(c); err == nil {
			return c, true
		}
	}
	return "", false
}

// FindCachedPackageArtifact returns the path to a cached .feather package
// for pkg+arch, or "" when none is present. The per-arch package store
// (packages/<arch>/<name>-<ver>-1-<arch>.feather) encodes name, version,
// and arch in the path, so a hit is already the right package — no need
// to crack the archive open to re-check.
func FindCachedPackageArtifact(b *builder.Builder, pkg *manifest.Package, targetArch string) string {
	return cachedArtifactPath(b.CacheDir, pkg.Package.Name, pkg.Package.Version, targetArch)
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
	manifestPath := filepath.Join(portsRoot, "base", name, "package.toml")
	pkg, err := manifest.LoadPackage(manifestPath)
	if err != nil {
		runner.Logf("Warning: skipping %s for initramfs (manifest load failed): %v\n", name, err)
		return ""
	}

	// Compute the build-dir path so we can reuse a previous in-chroot build
	// without re-running the full pipeline when only the .pkg.tar.gz is cached.
	_, chrootArch, err := resolveBuildOptions(pkg, targetArch, useQemuFlag, crossCompileFlag)
	if err != nil {
		runner.Logf("Warning: skipping %s for initramfs (resolveBuildOptions failed): %v\n", name, err)
		return ""
	}
	buildChrootDir := filepath.Join(workDir, "build-chroot", chrootArch)
	buildDirHint := filepath.Join(buildChrootDir, "build", fmt.Sprintf("%s-%s-%s", pkg.Package.Name, pkg.Package.Version, targetArch))

	if artifactPath := FindCachedPackageArtifact(b, pkg, targetArch); artifactPath != "" {
		if fileExists(buildDirHint) {
			runner.Logf("Reusing cached %s build dir at %s\n", name, buildDirHint)
			return buildDirHint
		}
		runner.Logf("Cached %s package present but build dir missing; rebuilding for initramfs\n", name)
	}

	runner.Logf("Building %s for initramfs...\n", name)
	buildDir, _, err := BuildPackageInChrootStep(b, pkg, targetArch, workDir, useQemuFlag, crossCompileFlag)
	if err != nil {
		runner.Logf("Warning: skipping %s for initramfs (build failed): %v\n", name, err)
		return ""
	}
	return buildDir
}

// BuildPackageInChrootStep performs the full chroot-build pipeline for a single
// package: resolve build options, ensure+bootstrap a build chroot for the right
// arch, stage build_dep_packages into it, run the build, and emit the final
// .pkg.tar.gz. It does NOT consult the artifact cache — callers that want to
// skip rebuilds should check FindCachedPackageArtifact first. Returns the
// in-chroot build directory and the produced artifact path.
func BuildPackageInChrootStep(b *builder.Builder, pkg *manifest.Package, targetArch, workDir, useQemuFlag, crossCompileFlag string) (string, string, error) {
	opts, chrootArch, err := resolveBuildOptions(pkg, targetArch, useQemuFlag, crossCompileFlag)
	if err != nil {
		return "", "", fmt.Errorf("resolving build options: %w", err)
	}
	buildChrootDir := filepath.Join(workDir, "build-chroot", chrootArch)
	buildDepChrootRoot := filepath.Join(workDir, "build-dep-chroot", builder.HostArchString())
	useQemu := opts.UseQemu != nil && *opts.UseQemu

	if err := b.EnsureBuildChroot(buildChrootDir, chrootArch, useQemu); err != nil {
		return "", "", fmt.Errorf("ensuring build chroot: %w", err)
	}
	if err := ensureBuildChrootBootstrap(b, buildChrootDir, chrootArch); err != nil {
		return "", "", fmt.Errorf("bootstrapping build chroot: %w", err)
	}
	extraPaths, err := prepareBuildDepPackages(b, pkg, targetArch, buildChrootDir, buildDepChrootRoot)
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

func prepareBuildDepPackages(b *builder.Builder, pkg *manifest.Package, consumingArch, chrootRoot, buildDepChrootRoot string) (preparedBuildDeps, error) {
	if len(pkg.Build.BuildDepPackages) == 0 {
		return preparedBuildDeps{}, nil
	}

	if err := ensureBuildDepBootstrap(b, buildDepChrootRoot); err != nil {
		return preparedBuildDeps{}, err
	}

	var extra preparedBuildDeps
	for _, depPkg := range pkg.Build.BuildDepPackages {
		candidates := []string{
			filepath.Join(portsRoot, "base", depPkg, "package.toml"),
			filepath.Join(portsRoot, "device", depPkg, "package.toml"),
		}
		var found string
		for _, c := range candidates {
			if _, err := os.Stat(c); err == nil {
				found = c
				break
			}
		}

		hostArch := builder.HostArchString()
		var depManifest *manifest.Package
		var artifactPath string

		switch {
		case found != "":
			// A port we build directly.
			pm, err := manifest.LoadPackage(found)
			if err != nil {
				return preparedBuildDeps{}, fmt.Errorf("failed to load build_dep_package %s: %w", depPkg, err)
			}
			// Pick the arch to build this dep for. A kernel declares
			// target_arch -> use it. A dep that needs a toolchain
			// (capabilities) but has no fixed target_arch is a device
			// artifact (e.g. busybox) -> build it for the consuming
			// device's arch, and set target_arch so its cross toolchain
			// resolves. Otherwise it's a host tool (toolchain, make) ->
			// hostArch.
			depArch := hostArch
			if pm.Build.TargetArch != "" {
				depArch = pm.Build.TargetArch
			} else if len(pm.Build.Capabilities) > 0 && consumingArch != "" {
				depArch = consumingArch
				pm.Build.TargetArch = consumingArch
			}
			artifactPath, err = buildOrCacheArtifact(b, pm, depPkg, depArch, buildDepChrootRoot)
			if err != nil {
				return preparedBuildDeps{}, err
			}
			depManifest = pm
		case strings.HasSuffix(depPkg, "-prp"):
			// A PRP-kernel subpackage: produced by the parent kernel port
			// (linux-<dev>), which emits linux-<dev>-prp.feather when it
			// declares prp_kernel_config.
			parent := strings.TrimSuffix(depPkg, "-prp")
			parentPath, ok := LocalPackageManifestPath(parent)
			if !ok {
				return preparedBuildDeps{}, fmt.Errorf("build_dep_package %s: no port, and no parent port %q", depPkg, parent)
			}
			pm, err := manifest.LoadPackage(parentPath)
			if err != nil {
				return preparedBuildDeps{}, err
			}
			if pm.Build.PRPKernelConfig == "" {
				return preparedBuildDeps{}, fmt.Errorf("build_dep_package %s: parent %q builds no -prp subpackage", depPkg, parent)
			}
			// Pick the arch to build this dep for. A kernel declares
			// target_arch -> use it. A dep that needs a toolchain
			// (capabilities) but has no fixed target_arch is a device
			// artifact (e.g. busybox) -> build it for the consuming
			// device's arch, and set target_arch so its cross toolchain
			// resolves. Otherwise it's a host tool (toolchain, make) ->
			// hostArch.
			depArch := hostArch
			if pm.Build.TargetArch != "" {
				depArch = pm.Build.TargetArch
			} else if len(pm.Build.Capabilities) > 0 && consumingArch != "" {
				depArch = consumingArch
				pm.Build.TargetArch = consumingArch
			}
			artifactPath, err = buildOrCacheArtifact(b, pm, depPkg, depArch, buildDepChrootRoot)
			if err != nil {
				return preparedBuildDeps{}, err
			}
			depManifest = pm
		default:
			return preparedBuildDeps{}, fmt.Errorf("build_dep_package %s not found in peacock-ports/base or peacock-ports/device", depPkg)
		}

		// Install the dep package into the build chroot via feather, at its
		// layout prefix (system -> /usr, peacock -> /peacock). The DB is
		// sandboxed under the chroot via FTR_DB_ROOT. The build then finds
		// the dep's tools on the standard PATH.
		ftr, err := builder.FtrBinary()
		if err != nil {
			return preparedBuildDeps{}, err
		}
		dbRoot := filepath.Join(chrootRoot, "var", "lib", "feather")
		if err := execCommand("sudo", "env", "FTR_DB_ROOT="+dbRoot,
			ftr, "install", "--root", chrootRoot, "--allow-unsigned", artifactPath); err != nil {
			return preparedBuildDeps{}, fmt.Errorf("failed to ftr-install build_dep_package %s: %w", depPkg, err)
		}

		// Wire the dep's install prefix onto the build env search paths so
		// its bin/lib/include are found (a no-op for /usr, already standard;
		// needed for /peacock-layout deps).
		prefix := depManifest.ResolvedPrefix()
		extra.Bin = appendUnique(extra.Bin, filepath.Join(prefix, "bin"), filepath.Join(prefix, "sbin"))
		extra.Inc = appendUnique(extra.Inc, filepath.Join(prefix, "include"))
		extra.Lib = appendUnique(extra.Lib, filepath.Join(prefix, "lib"), filepath.Join(prefix, "lib64"))
		extra.LD = appendUnique(extra.LD, filepath.Join(prefix, "lib"), filepath.Join(prefix, "lib64"))
	}

	return extra, nil
}

// buildOrCacheArtifact returns the cached .feather for wantPkg — building
// buildManifest (the port that produces it) if absent. wantPkg may differ
// from buildManifest's name when it's a subpackage (e.g. linux-<dev>-prp
// produced by the linux-<dev> port).
func buildOrCacheArtifact(b *builder.Builder, buildManifest *manifest.Package, wantPkg, hostArch, buildDepChrootRoot string) (string, error) {
	if p := cachedArtifactPath(b.CacheDir, wantPkg, buildManifest.Package.Version, hostArch); p != "" {
		return p, nil
	}
	buildDir, err := b.BuildPackageInChroot(buildManifest, hostArch, buildDepChrootRoot, builder.BuildOptions{
		UseQemu: boolPtr(false),
	})
	if err != nil {
		return "", fmt.Errorf("failed to build %s in chroot: %w", buildManifest.Package.Name, err)
	}
	if _, err := b.PackageArtifact(buildDir, buildManifest, hostArch); err != nil {
		return "", fmt.Errorf("failed to package %s: %w", buildManifest.Package.Name, err)
	}
	p := cachedArtifactPath(b.CacheDir, wantPkg, buildManifest.Package.Version, hostArch)
	if p == "" {
		return "", fmt.Errorf("build of %s did not produce subpackage %s", buildManifest.Package.Name, wantPkg)
	}
	return p, nil
}

func appendUnique(dst []string, vals ...string) []string {
	for _, v := range vals {
		seen := false
		for _, e := range dst {
			if e == v {
				seen = true
				break
			}
		}
		if !seen {
			dst = append(dst, v)
		}
	}
	return dst
}

func ensureBuildDepBootstrap(b *builder.Builder, root string) error {
	hostArch := builder.HostArchString()
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
	if chrootArch != builder.HostArchString() {
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
		} else if targetArch != builder.HostArchString() {
			val := true
			useQemu = &val
		} else {
			val := false
			useQemu = &val
		}
	}

	chrootArch := targetArch
	if useQemu != nil && !*useQemu {
		chrootArch = builder.HostArchString()
	}

	return builder.BuildOptions{
		UseQemu:      useQemu,
		CrossCompile: crossCompile,
		Flavor:       config.Flavor(),
	}, chrootArch, nil
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
