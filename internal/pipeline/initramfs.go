package pipeline

// Phase 3 of the build pipeline. Builds (or reuses cached)
// busybox + peacock-splash + msm-fb-refresher + util-linux + lvm2, locates
// the peacock-mkinitfs binary, and invokes it to produce the initramfs cpio.

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"peacock/internal/builder"
	"peacock/internal/manifest"
	"peacock/internal/runner"
)

// runInitramfsPhase performs phase 3 and returns the path to the produced
// initramfs cpio. Error is fatal; caller prints + cleans up.
func (r *Runner) runInitramfsPhase(
	b *builder.Builder,
	pkg *manifest.Package,
	dev *manifest.Device,
	depBuildDirs map[string]string,
	depPackagePaths map[string]string,
	initSystem string,
	workDir string,
) (string, error) {
	_ = pkg // currently unused; reserved for per-package initramfs hooks
	useQemuFlag := r.opts.UseQemu
	crossCompileFlag := r.opts.CrossCompile
	deviceName := r.opts.Device
	runner.Logln("Generating initramfs...")

	// Build Busybox (Generic)
	runner.Logln("Building/Fetching Busybox...")
	bbManifest := filepath.Join("peacock-ports", "base", "busybox", "package.toml")
	bbPkg, err := manifest.LoadPackage(bbManifest)
	if err != nil {
		return "", fmt.Errorf("error loading busybox manifest: %w", err)
	}

	busyboxBuildDir := ""
	busyboxCached := cachedArtifactPath(b.CacheDir, bbPkg.Package.Name, bbPkg.Package.Version, dev.Device.Architecture)
	if busyboxCached != "" && packageArchMatches(busyboxCached, pacmanArch(dev.Device.Architecture)) {
		extractedDir, err := extractBusyboxFromPackage(busyboxCached, workDir)
		if err != nil {
			return "", fmt.Errorf("error extracting busybox from cached package: %w", err)
		}
		busyboxBuildDir = extractedDir
		runner.Logf("Reusing busybox extracted from cached package at %s\n", busyboxBuildDir)
	}

	bbOpts, bbChrootArch, err := resolveBuildOptions(bbPkg, dev.Device.Architecture, useQemuFlag, crossCompileFlag)
	if err != nil {
		return "", fmt.Errorf("error resolving build options for busybox: %w", err)
	}
	bbChrootDir := filepath.Join(workDir, "build-chroot", bbChrootArch)
	buildDepChrootRoot := filepath.Join(workDir, "build-dep-chroot", builder.HostArchString())
	bbUseQemu := bbOpts.UseQemu != nil && *bbOpts.UseQemu
	if err := b.EnsureBuildChroot(bbChrootDir, bbChrootArch, bbUseQemu); err != nil {
		return "", fmt.Errorf("error ensuring build chroot for busybox: %w", err)
	}
	if err := ensureBuildChrootBootstrap(b, bbChrootDir, bbChrootArch); err != nil {
		return "", fmt.Errorf("error bootstrapping build tools for busybox: %w", err)
	}
	bbExtraPaths, err := prepareBuildDepPackages(b, bbPkg, bbChrootDir, buildDepChrootRoot)
	if err != nil {
		return "", fmt.Errorf("error preparing build dep packages for busybox: %w", err)
	}
	bbOpts.ExtraPath = bbExtraPaths.Bin
	bbOpts.ExtraInclude = bbExtraPaths.Inc
	bbOpts.ExtraLib = bbExtraPaths.Lib
	bbOpts.ExtraLdLib = bbExtraPaths.LD
	if busyboxBuildDir == "" {
		buildDir, err := b.BuildPackageInChroot(bbPkg, dev.Device.Architecture, bbChrootDir, bbOpts)
		if err != nil {
			return "", fmt.Errorf("error building busybox: %w", err)
		}
		if _, err := b.PackageArtifact(buildDir, bbPkg, dev.Device.Architecture); err != nil {
			return "", fmt.Errorf("error packaging busybox: %w", err)
		}
		busyboxBuildDir = buildDir
	}

	busyboxPath := filepath.Join(busyboxBuildDir, "busybox")
	if start, err := os.Stat(busyboxPath); err != nil || start.IsDir() {
		os.WriteFile(busyboxPath, []byte("BUSYBOX_BIN"), 0755)
	}

	// Build peacock-splash (for framebuffer splash screen)
	runner.Logln("Building/Fetching peacock-splash...")
	splashManifest := filepath.Join("peacock-ports", "base", "peacock-splash", "package.toml")
	splashPkg, err := manifest.LoadPackage(splashManifest)
	if err != nil {
		return "", fmt.Errorf("error loading peacock-splash manifest: %w", err)
	}

	splashBuildDir := ""
	splashCached := cachedArtifactPath(b.CacheDir, splashPkg.Package.Name, splashPkg.Package.Version, dev.Device.Architecture)
	if splashCached != "" && packageArchMatches(splashCached, pacmanArch(dev.Device.Architecture)) {
		tmpExtract, err := os.MkdirTemp("", "peacock-splash-extract-")
		if err == nil {
			defer os.RemoveAll(tmpExtract)
			cmd := exec.Command("tar", "-xzf", splashCached, "-C", tmpExtract)
			cmd.Stdout = runner.LogWriter()
			cmd.Stderr = runner.LogWriter()
			if runner.RunCmd(cmd) == nil {
				candidate := filepath.Join(tmpExtract, "usr", "bin", "peacock-splash")
				if _, err := os.Stat(candidate); err == nil {
					splashBuildDir = tmpExtract
					runner.Logf("Using cached peacock-splash package\n")
				}
			}
		}
	}

	splashOpts, splashChrootArch, err := resolveBuildOptions(splashPkg, dev.Device.Architecture, useQemuFlag, crossCompileFlag)
	if err != nil {
		return "", fmt.Errorf("error resolving build options for peacock-splash: %w", err)
	}
	splashChrootDir := filepath.Join(workDir, "build-chroot", splashChrootArch)
	splashUseQemu := splashOpts.UseQemu != nil && *splashOpts.UseQemu
	if err := b.EnsureBuildChroot(splashChrootDir, splashChrootArch, splashUseQemu); err != nil {
		return "", fmt.Errorf("error ensuring build chroot for peacock-splash: %w", err)
	}
	if err := ensureBuildChrootBootstrap(b, splashChrootDir, splashChrootArch); err != nil {
		return "", fmt.Errorf("error bootstrapping build tools for peacock-splash: %w", err)
	}
	splashExtraPaths, err := prepareBuildDepPackages(b, splashPkg, splashChrootDir, buildDepChrootRoot)
	if err != nil {
		return "", fmt.Errorf("error preparing build dep packages for peacock-splash: %w", err)
	}
	splashOpts.ExtraPath = splashExtraPaths.Bin
	splashOpts.ExtraInclude = splashExtraPaths.Inc
	splashOpts.ExtraLib = splashExtraPaths.Lib
	splashOpts.ExtraLdLib = splashExtraPaths.LD
	if splashBuildDir == "" {
		buildDir, err := b.BuildPackageInChroot(splashPkg, dev.Device.Architecture, splashChrootDir, splashOpts)
		if err != nil {
			return "", fmt.Errorf("error building peacock-splash: %w", err)
		}
		if _, err := b.PackageArtifact(buildDir, splashPkg, dev.Device.Architecture); err != nil {
			return "", fmt.Errorf("error packaging peacock-splash: %w", err)
		}
		splashBuildDir = buildDir
	}

	splashPath := filepath.Join(splashBuildDir, "usr", "bin", "peacock-splash")
	if _, err := os.Stat(splashPath); err != nil {
		altPath := filepath.Join(splashBuildDir, "stage", "usr", "bin", "peacock-splash")
		if _, err := os.Stat(altPath); err == nil {
			splashPath = altPath
		} else {
			runner.Logf("Warning: peacock-splash binary not found, initramfs will continue without splash\n")
			splashPath = ""
		}
	}

	refresherPath := ""
	useFBRefresher := dev.Quirks.UseFbRefresher
	if useFBRefresher {
		if cachedDir, ok := depBuildDirs["msm-fb-refresher"]; ok {
			candidate := filepath.Join(cachedDir, "usr", "bin", "msm-fb-refresher")
			if fileExistsFile(candidate) {
				refresherPath = candidate
			} else {
				altPath := filepath.Join(cachedDir, "stage", "usr", "bin", "msm-fb-refresher")
				if fileExistsFile(altPath) {
					refresherPath = altPath
				}
			}
		}
		if refresherPath == "" {
			if pkgPath, ok := depPackagePaths["msm-fb-refresher"]; ok {
				extractedDir, err := extractRefresherFromPackage(pkgPath, workDir)
				if err != nil {
					runner.Logf("Warning: failed to extract msm-fb-refresher: %v\n", err)
				} else {
					candidate := filepath.Join(extractedDir, "usr", "bin", "msm-fb-refresher")
					if fileExistsFile(candidate) {
						refresherPath = candidate
					} else {
						altPath := filepath.Join(extractedDir, "stage", "usr", "bin", "msm-fb-refresher")
						if fileExistsFile(altPath) {
							refresherPath = altPath
						}
					}
				}
			}
		}
		if refresherPath == "" {
			runner.Logf("Warning: msm-fb-refresher binary not found, initramfs will continue without refresher\n")
		}
	} else {
		runner.Logf("Info: skipping msm-fb-refresher for device %s\n", deviceName)
	}

	// Build util-linux + lvm2 ports for initramfs runtime tooling.
	utilLinuxBuildDir := buildPortForInitramfs(b, "util-linux", dev.Device.Architecture, workDir, useQemuFlag, crossCompileFlag)
	lvm2BuildDir := buildPortForInitramfs(b, "lvm2", dev.Device.Architecture, workDir, useQemuFlag, crossCompileFlag)

	// Build the peacock-mkinitfs port so its binary is available to invoke below.
	mkinitfsBuildDir := buildPortForInitramfs(b, "peacock-mkinitfs", dev.Device.Architecture, workDir, useQemuFlag, crossCompileFlag)
	mkinitfsBin := locatePeacockMkinitfs(mkinitfsBuildDir)
	if mkinitfsBin == "" {
		return "", fmt.Errorf("peacock-mkinitfs binary not found (port build dir empty and not on PATH). Install the peacock-mkinitfs port or `go install github.com/PeacockProject/peacock-mkinitfs/cmd/peacock-mkinitfs@latest`")
	}

	// Define root partition label for init script
	rootLabel := "ROOT"

	// Try to find resize2fs (for rootfs resizing in initramfs)
	resize2fsPath := ""
	for _, path := range []string{"/usr/sbin/resize2fs", "/sbin/resize2fs", "/usr/bin/resize2fs"} {
		if _, err := os.Stat(path); err == nil {
			resize2fsPath = path
			break
		}
	}

	initramfsPath := filepath.Join(workDir, "initramfs.cpio.gz")
	mkinitfsArgs := []string{
		"build",
		"--device", deviceName,
		"--arch", dev.Device.Architecture,
		"--init", initSystem,
		"--root-label", rootLabel,
		"--busybox", busyboxPath,
		"--output", initramfsPath,
	}
	if resize2fsPath != "" {
		mkinitfsArgs = append(mkinitfsArgs, "--resize2fs", resize2fsPath)
	}
	if splashPath != "" {
		mkinitfsArgs = append(mkinitfsArgs, "--splash", splashPath)
	}
	if refresherPath != "" {
		mkinitfsArgs = append(mkinitfsArgs, "--refresher", refresherPath)
	}
	if utilLinuxBuildDir != "" {
		mkinitfsArgs = append(mkinitfsArgs, "--util-linux", utilLinuxBuildDir)
	}
	if lvm2BuildDir != "" {
		mkinitfsArgs = append(mkinitfsArgs, "--lvm2", lvm2BuildDir)
	}
	mkinitfsCmd := exec.Command(mkinitfsBin, mkinitfsArgs...)
	mkinitfsCmd.Stdout = runner.LogWriter()
	mkinitfsCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(mkinitfsCmd); err != nil {
		return "", fmt.Errorf("error generating initramfs via %s: %w", mkinitfsBin, err)
	}
	runner.Logf("Initramfs generated at: %s\n", initramfsPath)

	return initramfsPath, nil
}
