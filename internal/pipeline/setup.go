package pipeline

// Phase 1 of the build pipeline. Validates flags, loads device + package
// manifests, dispatches the per-flavor bootstrap when not on arch, reads
// the resolved rootfs config (user, desktop, dm, extras) from viper, and
// initializes the Builder.
//
// Previously this lived in cmd/peacock/build_setup.go and pulled
// configuration from cobra-bound package-level globals + viper. Lifting
// it onto a Runner method (and dropping the interactive prompts here)
// makes the pipeline package usable from the Wails GUI without the
// cobra layer's TTY-only fallbacks.

import (
	"context"
	"fmt"
	"path/filepath"

	"peacock/internal/builder"
	"peacock/internal/config"
	"peacock/internal/manifest"
	"peacock/internal/runner"
)

// buildSetup is the bag of values phase 1 produces. The downstream
// phase functions consume individual fields.
type buildSetup struct {
	dev                  *manifest.Device
	pkg                  *manifest.Package
	b                    *builder.Builder
	cacheDir             string
	flavor               string
	initSystem           string
	desktopChoice        string
	displayManagerChoice string
	extraPackages        []string
	userName             string
	userPassword         string
	emptyRootfs          bool
}

// runBuildSetup performs phase 1. Any non-nil error is fatal; the caller
// should print + invoke the build cleanup before exiting.
//
// Unlike the cobra-era version, this does NOT prompt for missing values
// — pipeline package callers (GUI, automation) are expected to supply
// complete configs. The cobra `peacock build` Run wraps Runner.Run with
// its own prompt loop that populates viper before invoking the pipeline.
func (r *Runner) runBuildSetup(ctx context.Context, workDir string) (*buildSetup, error) {
	deviceName := r.opts.Device
	if deviceName == "" {
		return nil, fmt.Errorf("please specify a device with --device")
	}

	// Validate the requested base-distro flavor before doing anything
	// expensive. Phase 3 ships an arch implementation, a real debian
	// implementation (debootstrap + apt-get), and a real alpine
	// implementation (apk add --initdb + apk update). Non-arch paths
	// only cover the base-distro bootstrap step, with the rest of the
	// pipeline downstream still pacman-shaped.
	flavor := config.Flavor()
	if !config.IsValidFlavor(flavor) {
		return nil, fmt.Errorf("invalid flavor %q (valid: %v)", flavor, config.ValidFlavors)
	}
	runner.Logf("Base-distro flavor: %s\n", flavor)

	// Load device profile from peacock-ports
	devPath := filepath.Join("peacock-ports", "device", deviceName, "device.toml")
	dev, err := manifest.LoadDevice(devPath)
	if err != nil {
		return nil, fmt.Errorf("error loading device manifest %s: %w", devPath, err)
	}

	runner.Logf("Building for device: %s\n", dev.Device.Name)

	if flavor != "arch" {
		altRoot := filepath.Join(workDir, "flavor-root", flavor)
		if err := BootstrapBaseChroot(ctx, flavor, altRoot, dev.Device.Architecture, nil); err != nil {
			return nil, fmt.Errorf("bootstrap for flavor %q failed: %w", flavor, err)
		}
	}

	initSystem := config.InitSystem()
	if initSystem == "" {
		initSystem = "systemd" // default
	}
	desktopChoice := config.Desktop()
	displayManagerChoice := config.DisplayManager()
	extraPackages := config.ExtraPackages()
	userName := config.UserName()
	userPassword := config.UserPassword()
	emptyRootfs := config.EmptyRootfs()

	if emptyRootfs {
		runner.Logln("Empty-rootfs mode enabled: skipping rootfs package/user/desktop setup and producing a small debug image.")
		desktopChoice = "none"
		displayManagerChoice = "none"
		extraPackages = nil
		userName = ""
		userPassword = ""
	}

	// Load package manifest
	pkgPath := filepath.Join("peacock-ports", "device", deviceName, "package.toml")
	pkg, err := manifest.LoadPackage(pkgPath)
	if err != nil {
		return nil, fmt.Errorf("error loading package manifest %s: %w", pkgPath, err)
	}

	// Initialize Builder
	cacheDir := filepath.Join(workDir, "peacock-cache")
	b, err := builder.NewBuilder(cacheDir)
	if err != nil {
		return nil, fmt.Errorf("error initializing builder: %w", err)
	}

	return &buildSetup{
		dev:                  dev,
		pkg:                  pkg,
		b:                    b,
		cacheDir:             cacheDir,
		flavor:               flavor,
		initSystem:           initSystem,
		desktopChoice:        desktopChoice,
		displayManagerChoice: displayManagerChoice,
		extraPackages:        extraPackages,
		userName:             userName,
		userPassword:         userPassword,
		emptyRootfs:          emptyRootfs,
	}, nil
}
