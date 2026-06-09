package main

// Non-cobra entry point for the build pipeline. Used by the peacock-builder
// Wails GUI under cmd/peacock-builder/, which builds a buildconfig.BuildPipelineConfig
// from form state and calls RunBuildPipeline directly rather than spawning a
// `peacock build` subprocess. The cobra Run in build.go also funnels through
// here so the two paths stay in lockstep.

import (
	"context"
	"fmt"

	"github.com/spf13/viper"

	"peacock/internal/config"
	"peacock/pkg/buildconfig"
)

// RunBuildPipeline drives the five phase functions in order using cfg, and
// returns the path of the produced disk image. Errors are wrapped with the
// phase name so a GUI can surface a meaningful failure tag.
//
// It mutates a few process-global state holders to keep parity with the
// cobra path:
//   - the package-level cobra flag mirrors (deviceName, useQemuFlag,
//     crossCompileFlag, emptyRootfsFlag) — phase 1/3/4/5 read these directly.
//   - viper keys consumed by config.* accessors — runBuildSetup reads viper
//     for flavor, init system, desktop, etc.
//
// Sudo and runner-context wiring stay the caller's responsibility (the cobra
// Run sets them; the Wails caller will set its own MultiWriter + context).
func RunBuildPipeline(ctx context.Context, cfg buildconfig.BuildPipelineConfig) (string, error) {
	if err := cfg.Validate(); err != nil {
		return "", err
	}

	// Push the config into the same global state the existing cobra Run sets
	// up via pflag binding, so the phase functions see identical inputs no
	// matter who invoked us.
	deviceName = cfg.Device
	useQemuFlag = cfg.UseQemu
	if useQemuFlag == "" {
		useQemuFlag = "auto"
	}
	crossCompileFlag = cfg.CrossCompile
	emptyRootfsFlag = cfg.EmptyRootfs

	flavor := cfg.Flavor
	if flavor == "" {
		flavor = "arch"
	}
	viper.Set(config.KeyFlavor, flavor)
	viper.Set(config.KeyInitSystem, cfg.InitSystem)
	viper.Set(config.KeyDesktop, cfg.Desktop)
	viper.Set(config.KeyDisplayManager, cfg.DisplayManager)
	viper.Set(config.KeyExtraPackages, cfg.Extras)
	viper.Set(config.KeyUserName, cfg.UserName)
	viper.Set(config.KeyUserPassword, cfg.UserPassword)
	viper.Set(config.KeyImageSizeMB, cfg.ImageSizeMB)
	viper.Set(config.KeyEmptyRootfs, cfg.EmptyRootfs)
	if cfg.WorkDir != "" {
		viper.Set(config.KeyWorkDir, cfg.WorkDir)
	}

	workDir := cfg.WorkDir

	setup, err := runBuildSetup(ctx, workDir)
	if err != nil {
		return "", fmt.Errorf("build setup: %w", err)
	}

	pkgRes, err := runPackageOrchestration(
		setup.b, setup.pkg, setup.dev,
		setup.flavor, setup.initSystem, setup.desktopChoice, setup.displayManagerChoice,
		setup.extraPackages, workDir, useQemuFlag, crossCompileFlag,
	)
	if err != nil {
		return "", fmt.Errorf("package orchestration: %w", err)
	}

	initramfsPath, err := runInitramfsPhase(
		setup.b, setup.pkg, setup.dev,
		pkgRes.depBuildDirs, pkgRes.depPackagePaths,
		setup.initSystem, workDir, useQemuFlag, crossCompileFlag,
	)
	if err != nil {
		return "", fmt.Errorf("initramfs: %w", err)
	}

	// Cleanup tracker; phase 4 mutates it (image-build chroot mount) so the
	// signal handler / deferred Run in the cobra path can unmount cleanly.
	// In the GUI path no signal handler runs, but phase 4 still expects the
	// pointer to be non-nil.
	cleanup := &buildCleanup{workDir: workDir}

	rootfsRes, err := runRootfsPhase(
		setup.b, setup.pkg, setup.dev,
		pkgRes.depBuildDirs, pkgRes.depPackagePaths,
		pkgRes.pkgs, pkgRes.localPackages, setup.cacheDir,
		setup.initSystem, setup.desktopChoice, setup.displayManagerChoice,
		setup.userName, setup.userPassword, setup.emptyRootfs,
		initramfsPath, workDir, useQemuFlag, crossCompileFlag, cleanup,
	)
	if err != nil {
		return "", fmt.Errorf("rootfs: %w", err)
	}

	imagePath, err := runImageAssemblyPhase(
		setup.b, setup.dev,
		rootfsRes.imageChrootRoot, rootfsRes.rootfsPath, rootfsRes.kernelBuildDir,
		initramfsPath, setup.emptyRootfs, workDir,
	)
	if err != nil {
		return "", fmt.Errorf("image assembly: %w", err)
	}

	return imagePath, nil
}
