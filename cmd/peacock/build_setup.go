package main

// Phase 1 of the build pipeline. Called from `runBuild` in build.go before
// any other phase runs. Validates flags, loads device + package manifests,
// dispatches the per-flavor bootstrap when not on arch, gathers interactive
// rootfs prompts (user, desktop, dm, extras) and initializes the Builder.

import (
	"bufio"
	"context"
	"fmt"
	"os"
	"path/filepath"

	"peacock/internal/builder"
	"peacock/internal/config"
	"peacock/internal/manifest"
	"peacock/internal/userland"
)

// buildSetup is the bag of values phase 1 produces. The runBuild closure in
// build.go consumes it directly; downstream phases take individual fields.
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
	reader               *bufio.Reader
}

// runBuildSetup performs phase 1. Any non-nil error is fatal; the caller
// should print + invoke the build cleanup before exiting.
func runBuildSetup(ctx context.Context, workDir string) (*buildSetup, error) {
	if deviceName == "" {
		return nil, fmt.Errorf("please specify a device with --device")
	}

	// Validate the requested base-distro flavor before doing
	// anything expensive. Phase 3 ships an arch implementation, a
	// real debian implementation (debootstrap + apt-get), and a
	// real alpine implementation (apk add --initdb + apk update).
	// Non-arch paths only cover the base-distro bootstrap step,
	// with the rest of the pipeline downstream still pacman-shaped
	// (tracked in task.md "Flavor bootstrap").
	flavor := config.Flavor()
	if !config.IsValidFlavor(flavor) {
		return nil, fmt.Errorf("invalid flavor %q (valid: %v)", flavor, config.ValidFlavors)
	}
	fmt.Printf("Base-distro flavor: %s\n", flavor)

	// Load device profile from peacock-ports
	devPath := filepath.Join("peacock-ports", "device", deviceName, "device.toml")
	dev, err := manifest.LoadDevice(devPath)
	if err != nil {
		return nil, fmt.Errorf("error loading device manifest %s: %w", devPath, err)
	}

	fmt.Printf("Building for device: %s\n", dev.Device.Name)

	if flavor != "arch" {
		// debian: real debootstrap --foreign + qemu second-stage.
		// alpine: real apk add --initdb path.
		// Either way the rest of the build pipeline downstream is
		// still pacman-shaped (`InstallPackagesToRootfs`, etc.) so a
		// successful flavor bootstrap doesn't yet imply an end-to-end
		// build — that lands in later phases when the rootfs install
		// path also forks per flavor.
		altRoot := filepath.Join(workDir, "flavor-root", flavor)
		if err := bootstrapBaseChroot(ctx, flavor, altRoot, dev.Device.Architecture, nil); err != nil {
			return nil, fmt.Errorf("bootstrap for flavor %q failed: %w", flavor, err)
		}
	}

	initSystem := config.InitSystem()
	if initSystem == "" {
		initSystem = "systemd" // default
	}
	reader := bufio.NewReader(os.Stdin)
	desktopChoice := config.Desktop()
	displayManagerChoice := config.DisplayManager()
	extraPackages := config.ExtraPackages()
	userName := config.UserName()
	userPassword := config.UserPassword()
	emptyRootfs := config.EmptyRootfs()

	if emptyRootfs {
		fmt.Println("Empty-rootfs mode enabled: skipping rootfs package/user/desktop setup and producing a small debug image.")
		desktopChoice = "none"
		displayManagerChoice = "none"
		extraPackages = nil
		userName = ""
		userPassword = ""
	} else {
		if len(extraPackages) == 0 {
			extraPackages = promptCSV(reader, "Extra packages (comma-separated, empty for none)")
		}

		if desktopChoice == "" {
			fmt.Print(userland.DescribeChoices())
			desktopChoice = promptSelect(reader, "Desktop", userland.DesktopNames(), "none")
		}
		if displayManagerChoice == "" {
			displayManagerChoice = promptSelect(reader, "Display manager", userland.DisplayManagerNames(), "none")
		}

		if userName == "" {
			userName = promptLine(reader, "Username (empty to skip user creation)", "")
		}
		if userName != "" && userPassword == "" {
			userPassword = promptPassword(reader, "Password (plaintext)", "Confirm password")
		}
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
		reader:               reader,
	}, nil
}
