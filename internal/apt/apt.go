// Package apt is a phase-3 stub that mirrors the surface of
// internal/pacman so the build path can fork on flavor without compile
// errors. Every function returns "flavor not yet implemented" — the
// real apt integration lands in a later phase of the meta-distro
// migration.
package apt

import (
	"fmt"

	"peacock/internal/runner"
)

const flavor = "debian"

func notImplemented() error {
	fmt.Fprintf(runner.LogWriter(), "info: internal/apt: flavor %q not yet implemented (phase 3 stub)\n", flavor)
	return fmt.Errorf("flavor %q not yet implemented (phase 3 stub)", flavor)
}

// GenerateConfigContent mirrors pacman.GenerateConfigContent. The
// Debian equivalent is an /etc/apt/sources.list; we'll generate it in a
// future phase. For now we hand back an empty string and a stub error
// signaled out-of-band when the caller tries to act on it.
func GenerateConfigContent(arch string) string {
	_ = arch
	fmt.Fprintf(runner.LogWriter(), "info: internal/apt: GenerateConfigContent stubbed for arch=%q (phase 3 stub)\n", arch)
	return ""
}

// GenerateConfig is the apt analogue of pacman.GenerateConfig.
func GenerateConfig(target string, arch string) error {
	_ = target
	_ = arch
	return notImplemented()
}

// Install mirrors pacman.Install. Phase 3 returns the stub error; later
// phases will shell out to debootstrap / apt-get.
func Install(target string, configFile string, packages []string, cacheDir string, skipScripts bool, execRoot string) error {
	_ = target
	_ = configFile
	_ = packages
	_ = cacheDir
	_ = skipScripts
	_ = execRoot
	return notImplemented()
}

// InstallLocal mirrors pacman.InstallLocal.
func InstallLocal(target string, configFile string, packageFiles []string, cacheDir string, skipScripts bool, execRoot string) error {
	_ = target
	_ = configFile
	_ = packageFiles
	_ = cacheDir
	_ = skipScripts
	_ = execRoot
	return notImplemented()
}

// Bootstrap is the apt analogue of pacman.Bootstrap: keyring init +
// initial install. Stub for phase 3.
func Bootstrap(root string, packages []string) error {
	_ = root
	_ = packages
	return notImplemented()
}
