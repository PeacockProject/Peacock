// Package apk implements the Alpine flavor's base-distro bootstrap path
// for Peacock. It mirrors the surface of internal/pacman so the build
// path can fork on flavor without compile errors.
//
// The package shells out to Alpine's `apk` tool to populate a chroot at
// rootDir. The conventional invocation is:
//
//	apk add --root <rootDir> --initdb --arch <apk-arch> \
//	    --no-cache --update-cache \
//	    --repository <mirror>/<version>/main \
//	    alpine-base
//
// All commands run via internal/runner.RunCmd so cancellation, log
// routing, and process-group cleanup work the same way as the pacman
// path.
//
// Host prerequisites: the Peacock host needs an `apk` binary. The
// cleanest cross-host option is `apk.static` from
// https://gitlab.alpinelinux.org/alpine/apk-tools (the statically
// linked variant of apk that needs no Alpine userland to run). The
// package name varies by host distro:
//
//	Alpine: apk add apk-tools-static
//	Arch:   pacman -S apk-tools-static  (AUR)
//	Debian: build from source (see apk-tools README)
//
// findAPK() searches $PATH for `apk`, `apk.static`, `apk-tools-static`
// in that order. checkHostPrereqs() surfaces an actionable error early
// if none is found.
package apk

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
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

// ArchToApk is the exported wrapper so callers outside the package can
// translate Peacock arch → apk arch with a clear error on unknowns.
// Mirrors internal/apt.ArchToDpkg.
func ArchToApk(peacockArch string) (string, error) {
	a := archToApk(peacockArch)
	if a == "" {
		return "", fmt.Errorf("apk: unsupported peacock arch %q", peacockArch)
	}
	return a, nil
}

// findAPK searches $PATH for an apk binary. The order is:
//
//  1. apk            — present on Alpine hosts.
//  2. apk.static     — upstream static build.
//  3. apk-tools-static — Arch AUR / various distro packages.
//
// Returns the absolute path of the first match or an actionable error
// listing all three names and install hints.
func findAPK() (string, error) {
	candidates := []string{"apk", "apk.static", "apk-tools-static"}
	for _, name := range candidates {
		if p, err := exec.LookPath(name); err == nil {
			return p, nil
		}
	}
	return "", errMissingAPK(candidates)
}

// errMissingAPK builds the actionable "no apk on PATH" error message.
// Extracted so tests can assert on the wording without mutating $PATH.
func errMissingAPK(candidates []string) error {
	return fmt.Errorf(
		"no apk binary found on $PATH (looked for %s); install one of: "+
			"Alpine `apk add apk-tools-static`, "+
			"Arch (AUR) `pacman -S apk-tools-static`, "+
			"Debian: build apk-tools from source at https://gitlab.alpinelinux.org/alpine/apk-tools",
		strings.Join(candidates, ", "),
	)
}

// checkHostPrereqs is the actionable preflight: confirm an apk binary
// exists on the host before we kick off a bootstrap. The error message
// from findAPK is already actionable so we just forward it.
func checkHostPrereqs() error {
	_, err := findAPK()
	return err
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

// Bootstrap fills rootDir with an Alpine base system via
// `apk add --initdb`. The call is idempotent: if rootDir already looks
// like an Alpine root (i.e. /etc/alpine-release exists) we log a skip
// and return nil.
//
// cfg.Arch must be set to a non-empty apk arch (see archToApk).
func Bootstrap(rootDir string, cfg Config) error {
	cfg = cfg.withDefaults()
	if cfg.Arch == "" {
		return fmt.Errorf("apk.Bootstrap: cfg.Arch is required (use archToApk to translate)")
	}
	if rootDir == "" {
		return fmt.Errorf("apk.Bootstrap: rootDir is required")
	}

	apkBin, err := findAPK()
	if err != nil {
		return fmt.Errorf("apk.Bootstrap: %w", err)
	}
	fmt.Fprintf(runner.LogWriter(), "info: internal/apk: using %s\n", apkBin)

	if _, err := os.Stat(filepath.Join(rootDir, "etc", "alpine-release")); err == nil {
		fmt.Fprintf(runner.LogWriter(), "info: internal/apk: %s already looks like an Alpine root; skipping bootstrap\n", rootDir)
		return nil
	}

	if err := os.MkdirAll(rootDir, 0o755); err != nil {
		return fmt.Errorf("apk.Bootstrap: failed to create rootDir: %w", err)
	}

	mainRepo := fmt.Sprintf("%s/%s/main", cfg.Mirror, cfg.Version)
	args := []string{
		"add",
		"--root", rootDir,
		"--initdb",
		"--arch", cfg.Arch,
		"--no-cache",
		"--update-cache",
		"--repository", mainRepo,
		"--allow-untrusted",
		"alpine-base",
	}
	// apk's --initdb path requires root for chowning files under
	// rootDir; sudo here mirrors the pacman path's behavior.
	cmd := exec.Command("sudo", append([]string{apkBin}, args...)...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("apk.Bootstrap: %w", err)
	}
	return nil
}

// Setup writes /etc/apk/repositories inside the chroot and runs
// `apk update --root <rootDir>`. Equivalent of pacman.Bootstrap's
// keyring-init + sync tail (apk has no separate keyring step — its
// signature DB ships in alpine-keys which is pulled by alpine-base).
func Setup(rootDir string, cfg Config) error {
	cfg = cfg.withDefaults()
	if rootDir == "" {
		return fmt.Errorf("apk.Setup: rootDir is required")
	}
	apkBin, err := findAPK()
	if err != nil {
		return fmt.Errorf("apk.Setup: %w", err)
	}

	reposDir := filepath.Join(rootDir, "etc", "apk")
	if err := os.MkdirAll(reposDir, 0o755); err != nil {
		// Fall back to sudo for chroots we don't own.
		if mkErr := runner.Run("sudo", "mkdir", "-p", reposDir); mkErr != nil {
			return fmt.Errorf("apk.Setup: failed to create %s: %w (also tried sudo: %v)", reposDir, err, mkErr)
		}
	}

	content := GenerateConfigContent(cfg)
	reposPath := filepath.Join(reposDir, "repositories")
	// Write to a host-side temp first then move with sudo, so the
	// chroot file ends up root-owned without relying on the caller's
	// uid.
	tmp, err := os.CreateTemp("", "peacock-apk-repositories-*")
	if err != nil {
		return fmt.Errorf("apk.Setup: temp file: %w", err)
	}
	tmpPath := tmp.Name()
	defer os.Remove(tmpPath)
	if _, err := tmp.WriteString(content); err != nil {
		tmp.Close()
		return fmt.Errorf("apk.Setup: write temp: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("apk.Setup: close temp: %w", err)
	}
	if err := runner.Run("sudo", "cp", tmpPath, reposPath); err != nil {
		return fmt.Errorf("apk.Setup: install repositories file: %w", err)
	}
	if err := runner.Run("sudo", "chmod", "0644", reposPath); err != nil {
		return fmt.Errorf("apk.Setup: chmod repositories file: %w", err)
	}

	cmd := exec.Command("sudo", apkBin, "update", "--root", rootDir, "--no-cache", "--allow-untrusted")
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("apk.Setup: apk update: %w", err)
	}
	return nil
}

// resolveAliases rewrites build_deps entries through the
// peacock-ports/flavors/alpine/aliases.toml table. We duplicate a tiny
// loader here rather than importing internal/builder to keep the
// internal/apk → internal/builder edge from existing — builder already
// imports several lower-level packages and pulling apk into its graph
// would create a cycle once builder grows real apk-aware codepaths.
//
// Missing or unparsable table → identity passthrough with a one-shot
// warning, same policy as internal/builder.ResolveBuildDeps.
func resolveAliases(packages []string) []string {
	if len(packages) == 0 {
		return packages
	}
	path := filepath.Join("peacock-ports", "flavors", "alpine", "aliases.toml")
	data, err := os.ReadFile(path)
	if err != nil {
		fmt.Fprintf(runner.LogWriter(), "warning: apk.resolveAliases: %s not found (using identity map): %v\n", path, err)
		out := make([]string, len(packages))
		copy(out, packages)
		return out
	}
	table, err := parseAliasTable(data)
	if err != nil {
		fmt.Fprintf(runner.LogWriter(), "warning: apk.resolveAliases: %s parse error (using identity map): %v\n", path, err)
		out := make([]string, len(packages))
		copy(out, packages)
		return out
	}
	out := make([]string, 0, len(packages))
	for _, p := range packages {
		if alias, ok := table[p]; ok && alias != "" {
			out = append(out, alias)
			continue
		}
		out = append(out, p)
	}
	return out
}

// parseAliasTable is a deliberately minimal TOML reader scoped to the
// `[aliases]` section of flavors/<flavor>/aliases.toml. We avoid
// pulling go-toml in here so internal/apk stays
// dependency-light. Format reminder:
//
//	[aliases]
//	"base-devel" = "build-base"
//	"python" = "python3"
//
// Returns the alias map. Lines outside `[aliases]` and blank/comment
// lines inside are ignored. Mirrors internal/builder.loadFlavorAliases
// semantics — see the doc-comment on resolveAliases for why we
// re-implement instead of import.
func parseAliasTable(data []byte) (map[string]string, error) {
	out := map[string]string{}
	inAliases := false
	for lineNum, raw := range strings.Split(string(data), "\n") {
		line := strings.TrimSpace(raw)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			inAliases = strings.TrimSpace(line[1:len(line)-1]) == "aliases"
			continue
		}
		if !inAliases {
			continue
		}
		eq := strings.IndexByte(line, '=')
		if eq < 0 {
			return nil, fmt.Errorf("line %d: missing '=': %q", lineNum+1, line)
		}
		k := strings.TrimSpace(line[:eq])
		v := strings.TrimSpace(line[eq+1:])
		k = trimQuotes(k)
		v = trimQuotes(v)
		out[k] = v
	}
	return out, nil
}

func trimQuotes(s string) string {
	if len(s) >= 2 && (s[0] == '"' && s[len(s)-1] == '"') {
		return s[1 : len(s)-1]
	}
	if len(s) >= 2 && (s[0] == '\'' && s[len(s)-1] == '\'') {
		return s[1 : len(s)-1]
	}
	return s
}

// Install adds packages into an existing Alpine root via
// `apk add --root <rootDir> --no-cache`. Package names pass through
// the alpine alias table first so manifests written against the Arch
// canonical names (base-devel, python, ncurses, ...) translate to
// Alpine's names (build-base, python3, ncurses-dev, ...).
//
// Idempotent: apk add on an already-installed package is a no-op.
func Install(rootDir string, packages []string) error {
	if len(packages) == 0 {
		return nil
	}
	if rootDir == "" {
		return fmt.Errorf("apk.Install: rootDir is required")
	}
	apkBin, err := findAPK()
	if err != nil {
		return fmt.Errorf("apk.Install: %w", err)
	}

	resolved := resolveAliases(packages)
	args := append([]string{apkBin, "add", "--root", rootDir, "--no-cache"}, resolved...)
	cmd := exec.Command("sudo", args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("apk.Install: %w", err)
	}
	return nil
}

// --- pacman-surface compatibility shims ----------------------------------
//
// These keep the surface symmetric with internal/pacman for the
// downstream pipeline (which still has pacman-shaped callsites the
// alpine work hasn't reached yet).

// GenerateConfig writes /etc/apk/repositories for `arch` (Peacock arch
// naming) inside target. Equivalent of pacman.GenerateConfig — uses
// defaults for version/mirror/branches.
func GenerateConfig(target string, arch string) error {
	cfg := Config{Arch: archToApk(arch)}
	if cfg.Arch == "" {
		return fmt.Errorf("apk.GenerateConfig: unsupported arch %q", arch)
	}
	dir := filepath.Join(target, "etc", "apk")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(dir, "repositories"), []byte(GenerateConfigContent(cfg)), 0o644)
}

// InstallLocal installs local .apk files into the chroot. apk treats
// `add <file.apk>` the same as `add <name>` so the implementation is
// just a thin wrapper around Install with --allow-untrusted toggled on
// (local files won't be signed by the upstream key).
func InstallLocal(target string, packageFiles []string) error {
	if len(packageFiles) == 0 {
		return nil
	}
	apkBin, err := findAPK()
	if err != nil {
		return fmt.Errorf("apk.InstallLocal: %w", err)
	}
	args := append([]string{apkBin, "add", "--root", target, "--no-cache", "--allow-untrusted"}, packageFiles...)
	cmd := exec.Command("sudo", args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("apk.InstallLocal: %w", err)
	}
	return nil
}
