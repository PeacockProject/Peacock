// Package buildconfig defines the exported configuration struct that drives
// the Peacock build pipeline from a non-cobra entry point (notably the
// peacock-builder Wails GUI under cmd/peacock-builder/).
//
// The cobra `peacock build` command reads viper into one of these and hands
// it to cmd/peacock.RunBuildPipeline; a GUI builds the same struct from form
// state and calls the same wrapper. Keeping the type in pkg/ (rather than
// inside the main package at cmd/peacock/) lets the Wails binary import it
// without dragging in cobra/viper or the rest of the CLI's main package.
//
// The struct is intentionally flat: each field maps 1:1 to a cobra flag /
// viper key on the CLI side. Adding a flag means adding a field here and
// wiring it in both the cobra Run and the GUI form.
package buildconfig

import (
	"fmt"

	"peacock/internal/config"
)

// BuildPipelineConfig is the full input to a single end-to-end build run.
// Every field a cobra flag previously set lives here; the wrapper in
// cmd/peacock (RunBuildPipeline) pushes them into viper before invoking the
// phase functions so the existing phase code keeps reading config the same way.
type BuildPipelineConfig struct {
	// Device is the codename of the target device under peacock-ports/device/.
	// Required. Drives device.toml + linux-<device> port lookups.
	Device string

	// Flavor selects the base-distro bootstrap. One of config.ValidFlavors
	// ("arch", "debian", "alpine"). Required; defaults to "arch" on the CLI.
	Flavor string

	// InitSystem is "systemd" or "openrc". Selects which Dependencies* list
	// from the device package manifest is consumed by phase 2.
	InitSystem string

	// Desktop is a userland desktop choice ("none", "xfce", "lxqt", ...).
	// Empty string defers to the interactive prompt in runBuildSetup; GUI
	// callers should set this explicitly to bypass the prompt.
	Desktop string

	// DisplayManager is a display-manager choice ("none", "lightdm", ...).
	// Same prompt semantics as Desktop above.
	DisplayManager string

	// Extras is the list of additional packages to install into the rootfs
	// beyond the device manifest's resolved deps and the userland selection.
	Extras []string

	// UserName is the username to create inside the rootfs. Empty string
	// skips user creation. The cobra Run prompts when empty + interactive.
	UserName string

	// UserPassword is the plaintext password for UserName. Ignored when
	// UserName is empty. The cobra Run prompts when missing + interactive.
	UserPassword string

	// ImageSizeMB requests a specific disk image size in megabytes.
	// 0 means auto-size from rootfs contents (the common case).
	ImageSizeMB int

	// EmptyRootfs requests a small debug image with boot assets only and
	// an empty labeled root partition. Useful for kernel-bringup work.
	// When true, Desktop/DisplayManager/Extras/UserName/UserPassword are
	// ignored.
	EmptyRootfs bool

	// UseQemu selects qemu-user emulation for foreign-arch builds:
	// "auto" (default), "true" (force qemu), or "false" (disable qemu and
	// rely on native or cross-compile). Mirrors --use-qemu on the CLI.
	UseQemu string

	// CrossCompile is an optional cross compiler prefix (e.g.
	// "arm-none-eabi-"). Mirrors --cross-compile on the CLI. Empty string
	// defers to the per-port manifest's build.cross_compile value.
	CrossCompile string

	// WorkDir is the absolute path of the Peacock work directory (where
	// chroots, caches, logs, and the final image land). Required; the CLI
	// reads it from `peacock init`'s persisted config.
	WorkDir string

	// Architecture is the target architecture for the build (e.g.
	// "aarch64"). Normally inferred from the device manifest; kept here as
	// an override for unusual cross-build scenarios.
	Architecture string
}

// Validate checks that the required fields are populated and that the chosen
// flavor is one Peacock can build. It does not touch the filesystem; deeper
// validation (device manifest existence, workDir writability, etc.) happens
// inside the phase functions.
func (c *BuildPipelineConfig) Validate() error {
	if c == nil {
		return fmt.Errorf("buildconfig: nil config")
	}
	if c.Device == "" {
		return fmt.Errorf("buildconfig: Device is required")
	}
	if c.WorkDir == "" {
		return fmt.Errorf("buildconfig: WorkDir is required")
	}
	if c.Flavor != "" && !config.IsValidFlavor(c.Flavor) {
		return fmt.Errorf("buildconfig: invalid Flavor %q (valid: %v)", c.Flavor, config.ValidFlavors)
	}
	return nil
}
