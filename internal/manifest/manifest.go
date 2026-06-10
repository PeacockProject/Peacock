package manifest

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/pelletier/go-toml/v2"
)

// Device represents the device.toml structure
type Device struct {
	Device struct {
		Name         string `toml:"name"`
		Architecture string `toml:"architecture"`
		FlashMethod  string `toml:"flash_method"`
		SoC          string `toml:"soc"`
		// Status is the port maturity. Recognised values:
		//   "stable"       — daily-driveable
		//   "testing"      — mostly works
		//   "experimental" — basic boot, many features missing
		//   "partial"      — only some features work
		//   "unsupported"  — listed for reference only
		Status string `toml:"status"`
	} `toml:"device"`

	// Support is a free-form map of feature → state, surfaced by the
	// peacock-builder GUI's "What works on this device" matrix.
	// Conventional keys: calls, sms, wifi, bluetooth, touch, gpu,
	// battery, audio, camrear, camfront, gps, sensors, modem. State
	// values: "ok" | "partial" | "none". Notes go in <key>_note.
	Support map[string]string `toml:"support"`

	Quirks struct {
		KeepFbRefresherWithDM bool `toml:"keep_fb_refresher_with_dm"`
		XorgForceVT1          bool `toml:"xorg_force_vt1"`
		UseFbRefresher        bool `toml:"use_fb_refresher"`
		LegacyRootfsExt4      bool `toml:"legacy_rootfs_ext4"`
	} `toml:"quirks"`

	Boot struct {
		GenerateBootImg bool   `toml:"generate_bootimg"`
		Cmdline         string `toml:"cmdline"`
		Android         struct {
			PageSize      int    `toml:"page_size"`
			Base          string `toml:"base"`
			KernelOffset  string `toml:"kernel_offset"`
			RamdiskOffset string `toml:"ramdisk_offset"`
			SecondOffset  string `toml:"second_offset"`
			TagsOffset    string `toml:"tags_offset"`
		} `toml:"android"`
	} `toml:"boot"`
}

// Install captures the [install] table introduced in Phase 1 of the
// meta-distro migration. All fields are optional; absent in TOML means
// zero-value here, and ResolvedLayout / ResolvedPrefix on Package fill
// the gaps with sensible defaults.
type Install struct {
	Layout string   `toml:"layout"`
	Prefix string   `toml:"prefix"`
	Files  []string `toml:"files"`
}

// Package represents a package.toml metdata
type Package struct {
	Package struct {
		Name        string   `toml:"name"`
		Version     string   `toml:"version"`
		Description string   `toml:"description"`
		Provides    []string `toml:"provides"`
		Depends     []string `toml:"depends"`
		Flavor      []string `toml:"flavor"`
		Runtime     string   `toml:"runtime"`
	} `toml:"package"`

	Build struct {
		Dependencies        []string `toml:"dependencies"`
		DependenciesOpenRC  []string `toml:"dependencies_openrc"`
		DependenciesSystemd []string `toml:"dependencies_systemd"`
		BuildDeps           []string `toml:"build_deps"`
		BuildDepPackages    []string `toml:"build_dep_packages"`
		UseQemu             *bool    `toml:"use_qemu"`
		CrossCompile        string   `toml:"cross_compile"`
		// TargetArch is the architecture this port's artifacts target
		// (e.g. "aarch64" for a kernel). Drives capability resolution
		// (which triple / cross toolchain) and the derived CROSS_COMPILE.
		// Empty = host-native build.
		TargetArch string `toml:"target_arch"`
		// Capabilities are abstract build requirements (e.g. "c-toolchain")
		// resolved per build mode × target arch × flavor via
		// peacock-ports/toolchains.toml. Ports declare what they need;
		// Peacock installs the right distro packages. See
		// docs/design/toolchain-capabilities.md.
		Capabilities []string `toml:"capabilities"`
		// Triple overrides the GNU triple looked up from target_arch (e.g.
		// a port that needs "arm-eabi" rather than the standard
		// "arm-linux-gnueabihf"). Flows into both package resolution and
		// the derived CROSS_COMPILE so they can't disagree.
		Triple string `toml:"triple"`
		// Integrate controls where this package lands when staged as a
		// build_dep_package (a port we build ourselves — device-specific or
		// not in the distro). Default (false) keeps it in the /peacock
		// overlay (the feather domain); true integrates it into the base
		// system tree at /usr. Either way its bin/lib/include are wired
		// into the build env (PATH / LD_LIBRARY_PATH).
		Integrate bool `toml:"integrate"`
		// KernelConfig / PRPKernelConfig name the in-port config files a
		// kernel build script consumes, exposed to the script as
		// $KERNEL_CONFIG / $PRP_KERNEL_CONFIG. A kernel port that sets
		// PRPKernelConfig builds a second, PRP-trimmed kernel in the same
		// source tree (staged as zImage-prp), so the recovery reuses the
		// already-downloaded source + toolchain instead of a separate
		// linux-<dev>-prp port. Empty = no kernel config / no PRP variant.
		KernelConfig    string `toml:"kernel_config"`
		PRPKernelConfig string `toml:"prp_kernel_config"`
		// Type selects the default build phase set (lib/build/<type>.sh):
		// raw | make | autotools | kernel. Empty defaults to raw. A port's
		// build.sh overrides individual phases. Replaces inline Script.
		Type string `toml:"type"`
		// Phase-default knobs consumed by the lib/build phases, exposed to
		// the build as $-vars of the same name.
		Prefix          string `toml:"prefix"`
		ConfigureArgs   string `toml:"configure_args"`
		MakeArgs        string `toml:"make_args"`
		MakeInstallArgs string `toml:"make_install_args"`
		Patches         string `toml:"patches"`
		Strip           string `toml:"strip"`
		// Script is the legacy inline build script. Deprecated and being
		// migrated to build.sh + Type; retained only until the sweep
		// completes so unconverted ports keep building.
		Script   string `toml:"script"`
		Source   string `toml:"source"`
		Checksum string `toml:"checksum"`
	} `toml:"build"`

	Install Install `toml:"install"`

	// Provides / Conflicts are optional capability tables. The TOML
	// shape is `capability = "version-or-glob"`. Absent in source =
	// nil map here; existing callers that don't read these are
	// unaffected.
	Provides  map[string]string `toml:"provides"`
	Conflicts map[string]string `toml:"conflicts"`

	ManifestDir string // Directory containing package.toml, not loaded from TOML
}

// ResolvedLayout returns the install layout (system | peacock | app |
// compat). Defaults to "system" so the 51 existing ports that don't yet
// carry an [install] table behave as before.
func (p *Package) ResolvedLayout() string {
	if p == nil {
		return "system"
	}
	if p.Install.Layout != "" {
		return p.Install.Layout
	}
	return "system"
}

// ResolvedPrefix returns the on-disk overlay root for the package. If
// the manifest sets [install].prefix explicitly we use it verbatim;
// otherwise we derive a sensible default from the layout per the
// meta-distro plan.
func (p *Package) ResolvedPrefix() string {
	if p == nil {
		return "/usr"
	}
	if p.Install.Prefix != "" {
		return p.Install.Prefix
	}
	switch p.ResolvedLayout() {
	case "system":
		return "/usr"
	case "peacock":
		return "/peacock"
	case "app":
		return "/apps/" + p.Package.Name
	case "compat":
		rt := p.Package.Runtime
		if rt == "" {
			rt = "unknown"
		}
		return "/compat/" + rt
	default:
		return "/usr"
	}
}

// SupportsFlavor reports whether this port participates in builds for
// the given base-distro flavor. A manifest that omits `flavor` lists
// all flavors implicitly.
func (p *Package) SupportsFlavor(name string) bool {
	if p == nil || len(p.Package.Flavor) == 0 {
		return true
	}
	for _, f := range p.Package.Flavor {
		if f == name {
			return true
		}
	}
	return false
}

// LoadPackage loads a package.toml
func LoadPackage(path string) (*Package, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read package manifest: %w", err)
	}

	var pkg Package
	if err := toml.Unmarshal(data, &pkg); err != nil {
		return nil, fmt.Errorf("failed to parse package manifest: %w", err)
	}

	pkg.ManifestDir = filepath.Dir(path)
	return &pkg, nil
}

// LoadDevice loads a device.toml
func LoadDevice(path string) (*Device, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read device manifest: %w", err)
	}

	var dev Device
	if err := toml.Unmarshal(data, &dev); err != nil {
		return nil, fmt.Errorf("failed to parse device manifest: %w", err)
	}

	return &dev, nil
}
