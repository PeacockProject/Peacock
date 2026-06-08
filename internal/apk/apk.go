// Package apk implements the Alpine flavor's base-distro bootstrap path
// for Peacock. It mirrors the surface of internal/pacman so the build
// path can fork on flavor without compile errors.
//
// This commit lands the configuration scaffolding only:
//
//   - Version constants (V3_18 / V3_19 / V3_20 / Edge) and DefaultVersion.
//   - DefaultMirror / DefaultBranches for the apk repositories file.
//   - Config{Version, Arch, Mirror, Branches} with sensible defaults.
//   - archToApk(): Peacock arch → apk arch translation, with a sibling
//     shape to internal/apt's archToDpkg.
//   - GenerateConfigContent(): emits the /etc/apk/repositories body
//     used by Setup.
//
// Bootstrap / Setup / Install land in follow-up commits; the stub
// surface is preserved here so the build path keeps compiling.
package apk

import (
	"fmt"
	"strings"

	"peacock/internal/runner"
)

const flavor = "alpine"

// Version is the Alpine release branch used when generating
// /etc/apk/repositories and when bootstrapping the chroot. We only
// expose the major branches Peacock cares about; --alpine-version flag
// wiring is out of scope for phase 3.
type Version string

const (
	V3_18 Version = "v3.18"
	V3_19 Version = "v3.19"
	V3_20 Version = "v3.20"
	Edge  Version = "edge"

	// DefaultVersion is the Version constant new Configs pick up when
	// the caller leaves cfg.Version empty.
	DefaultVersion = V3_20

	// DefaultMirror is the upstream Alpine CDN. Cosmetic mirrors (e.g.
	// dl-3, dl-4) work as drop-in replacements but the CDN is fine for
	// CI / occasional developer builds.
	DefaultMirror = "https://dl-cdn.alpinelinux.org/alpine"
)

// DefaultBranches is the standard pair of repositories we enable.
// Community holds most build deps Peacock cares about (gcc, qt6-base,
// etc.) that aren't in main.
var DefaultBranches = []string{"main", "community"}

// Config carries the knobs the Bootstrap / Setup paths need. Every
// field has a sensible default; callers can leave any of them zero.
type Config struct {
	// Version is the Alpine branch (v3.18, v3.20, edge, ...). Defaults
	// to DefaultVersion if empty.
	Version Version
	// Arch is the apk-side architecture name (aarch64, armv7,
	// x86_64). Use archToApk to translate from Peacock's arch naming.
	Arch string
	// Mirror is the URL prefix that <mirror>/<version>/<branch> is
	// composed against. Defaults to DefaultMirror if empty.
	Mirror string
	// Branches is the ordered list of repositories to enable. Defaults
	// to DefaultBranches if empty.
	Branches []string
}

func (c Config) withDefaults() Config {
	if c.Version == "" {
		c.Version = DefaultVersion
	}
	if c.Mirror == "" {
		c.Mirror = DefaultMirror
	}
	if len(c.Branches) == 0 {
		c.Branches = append([]string{}, DefaultBranches...)
	}
	return c
}

// archToApk maps Peacock's architecture names onto apk's. The Debian
// side of the meta-distro has a sibling archToDpkg that performs the
// same shape of mapping; the two helpers should look obviously paired
// even though they live in separate packages.
//
// Returns "" for unknown architectures so callers can produce a clear
// "unsupported arch" error at the call site rather than passing junk
// down to apk.
func archToApk(peacockArch string) string {
	switch peacockArch {
	case "aarch64":
		return "aarch64"
	case "armv7", "armv7h":
		return "armv7"
	case "x86_64":
		return "x86_64"
	default:
		return ""
	}
}

// GenerateConfigContent returns the contents of an /etc/apk/repositories
// file for the given Config. One line per branch, in the form
// "<mirror>/<version>/<branch>".
//
// Equivalent of pacman.GenerateConfigContent for the Alpine flavor.
func GenerateConfigContent(cfg Config) string {
	cfg = cfg.withDefaults()
	var b strings.Builder
	for _, branch := range cfg.Branches {
		fmt.Fprintf(&b, "%s/%s/%s\n", cfg.Mirror, cfg.Version, branch)
	}
	return b.String()
}

// --- pacman-surface compatibility shims ----------------------------------
//
// These keep the surface symmetric with internal/pacman so the build
// path can keep compiling while Bootstrap / Setup / Install land in
// follow-up commits.

func notImplemented() error {
	fmt.Fprintf(runner.LogWriter(), "info: internal/apk: flavor %q not yet implemented (phase 3 stub)\n", flavor)
	return fmt.Errorf("flavor %q not yet implemented (phase 3 stub)", flavor)
}

// GenerateConfig is the apk analogue of pacman.GenerateConfig. Stubbed
// until Setup lands.
func GenerateConfig(target string, arch string) error {
	_ = target
	_ = arch
	return notImplemented()
}

// Install mirrors pacman.Install. Stub.
func Install(target string, configFile string, packages []string, cacheDir string, skipScripts bool, execRoot string) error {
	_ = target
	_ = configFile
	_ = packages
	_ = cacheDir
	_ = skipScripts
	_ = execRoot
	return notImplemented()
}

// InstallLocal mirrors pacman.InstallLocal. Stub.
func InstallLocal(target string, configFile string, packageFiles []string, cacheDir string, skipScripts bool, execRoot string) error {
	_ = target
	_ = configFile
	_ = packageFiles
	_ = cacheDir
	_ = skipScripts
	_ = execRoot
	return notImplemented()
}

// Bootstrap is the apk analogue of pacman.Bootstrap. Stub.
func Bootstrap(root string, packages []string) error {
	_ = root
	_ = packages
	return notImplemented()
}
