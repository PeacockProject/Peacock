package pipeline

// flashset.go builds a device's flashable set — the bootloader and PRP
// recovery images (plus any PRP-specific kernel) — as a distinct target
// from the system rootfs/image build. The flash flow triggers this on
// top of the normal build; `peacock build` alone produces only the
// system image. Build order honors the dependency chain: a PRP-specific
// kernel (when the device has one) builds before the PRP recovery image
// that bundles it; the bootloader is independent.

import (
	"fmt"
	"path/filepath"

	"peacock/internal/builder"
	"peacock/internal/manifest"
	"peacock/internal/ports"
	"peacock/internal/runner"
)

// FlashSetArtifacts holds the staged image paths produced by
// BuildFlashSet. Empty fields mean the device has no such port.
type FlashSetArtifacts struct {
	Bootloader string // path to the staged bootloader boot.img
	Recovery   string // path to the staged PRP recovery .img
}

// FlashSetProgress mirrors RunnerOpts.Progress: a phase label + percent,
// called at each flashable-set build boundary. nil = no-op.
type FlashSetProgress func(phase string, percent int)

// BuildFlashSet builds the bootloader and PRP recovery for device, in
// order. It resolves the port names from peacock-ports (ensuring the
// tree is present), constructs its own Builder under workDir, and builds
// each port through the same chroot machinery as the rest of the
// pipeline. useQemuFlag/crossCompileFlag are threaded so the
// per-port use_qemu/cross_compile settings are honored (pass "auto").
//
// Devices with no bootloader and no recovery (PinePhone, x86) return
// empty artifacts and no error — there's nothing flashable beyond the
// system image.
func BuildFlashSet(device, workDir, useQemuFlag, crossCompileFlag string, progress FlashSetProgress) (FlashSetArtifacts, error) {
	emit := func(phase string, pct int) {
		if progress != nil {
			progress(phase, pct)
		}
	}

	// Make sure peacock-ports is present + thread the resolved root so
	// LocalPackageManifestPath and friends resolve.
	root, err := ports.Ensure()
	if err != nil {
		return FlashSetArtifacts{}, fmt.Errorf("flashset: peacock-ports: %w", err)
	}
	SetPortsRoot(root)

	set, ok := ports.ResolveFlashSet(device)
	if !ok {
		return FlashSetArtifacts{}, fmt.Errorf("flashset: cannot resolve ports for %q", device)
	}
	if set.Empty() {
		runner.Logf("No bootloader/recovery ports for %s — nothing to build beyond the system image.\n", device)
		emit("Flashables ready", 100)
		return FlashSetArtifacts{}, nil
	}

	// Target arch comes from the device manifest.
	devPath := filepath.Join(root, "device", device, "device.toml")
	dev, err := manifest.LoadDevice(devPath)
	if err != nil {
		return FlashSetArtifacts{}, fmt.Errorf("flashset: load device %s: %w", device, err)
	}
	targetArch := dev.Device.Architecture

	cacheDir := filepath.Join(workDir, "peacock-cache")
	b, err := builder.NewBuilder(cacheDir)
	if err != nil {
		return FlashSetArtifacts{}, fmt.Errorf("flashset: builder: %w", err)
	}

	var arts FlashSetArtifacts

	// 1. PRP-specific kernel first (only some devices). It's a build_dep
	//    of the recovery port, but building it explicitly up front gives
	//    the user a clear "Building kernel" phase and warms the cache.
	if set.PRPKernel != "" {
		emit("Building kernel", 10)
		if _, err := buildOnePort(b, set.PRPKernel, targetArch, workDir, useQemuFlag, crossCompileFlag); err != nil {
			return arts, fmt.Errorf("flashset: PRP kernel %s: %w", set.PRPKernel, err)
		}
	}

	// 2. Bootloader (minkernel / lk2nd).
	if set.Bootloader != "" {
		emit("Building bootloader", 40)
		buildDir, err := buildOnePort(b, set.Bootloader, targetArch, workDir, useQemuFlag, crossCompileFlag)
		if err != nil {
			return arts, fmt.Errorf("flashset: bootloader %s: %w", set.Bootloader, err)
		}
		arts.Bootloader = findStagedImage(buildDir, "bootloaders")
	}

	// 3. PRP recovery.
	if set.Recovery != "" {
		emit("Building recovery", 70)
		buildDir, err := buildOnePort(b, set.Recovery, targetArch, workDir, useQemuFlag, crossCompileFlag)
		if err != nil {
			return arts, fmt.Errorf("flashset: recovery %s: %w", set.Recovery, err)
		}
		arts.Recovery = findStagedImage(buildDir, "recovery")
	}

	emit("Flashables ready", 100)
	return arts, nil
}

// buildOnePort loads a device-tree port by name and builds it through the
// shared chroot step, returning its build dir.
func buildOnePort(b *builder.Builder, name, targetArch, workDir, useQemuFlag, crossCompileFlag string) (string, error) {
	manifestPath, ok := LocalPackageManifestPath(name)
	if !ok {
		return "", fmt.Errorf("port %q not found under peacock-ports", name)
	}
	pkg, err := manifest.LoadPackage(manifestPath)
	if err != nil {
		return "", fmt.Errorf("load %s: %w", name, err)
	}
	buildDir, _, err := BuildPackageInChrootStep(b, pkg, targetArch, workDir, useQemuFlag, crossCompileFlag)
	if err != nil {
		return "", err
	}
	return buildDir, nil
}

// findStagedImage globs for the first *.img a bootloader/recovery port
// stages under stage/usr/share/peacock/<subdir>/. Returns "" when none —
// the caller decides whether that's fatal.
func findStagedImage(buildDir, subdir string) string {
	if buildDir == "" {
		return ""
	}
	pattern := filepath.Join(buildDir, "stage", "usr", "share", "peacock", subdir, "*.img")
	matches, _ := filepath.Glob(pattern)
	if len(matches) > 0 {
		return matches[0]
	}
	return ""
}
