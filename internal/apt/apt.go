// Package apt provides the real Debian/apt bootstrap path for Peacock's
// `--flavor debian`. It mirrors the surface of internal/pacman so the
// build-path fork in cmd/peacock/flavor.go can call into it without
// special-casing.
//
// Host prerequisites (must be on PATH for Bootstrap to succeed):
//
//   - debootstrap          (initial chroot fill)
//   - qemu-<arch>-static   (for foreign-arch second-stage on x86_64 hosts;
//                           the binary name is derived from the target arch,
//                           e.g. qemu-aarch64-static for arm64)
//
// Install instructions per host:
//
//   - Debian/Ubuntu:  sudo apt install debootstrap qemu-user-static
//   - Arch Linux:     sudo pacman -S debootstrap qemu-user-static-binfmt
//                     (qemu-user-static-bin is also available via AUR)
//
// Implementation outline:
//
//   - Bootstrap runs `debootstrap --foreign --variant=minbase --arch=<dpkg>`
//     for the requested suite, copies the matching qemu-user-static binary
//     into <rootDir>/usr/bin/, then runs `chroot <rootDir>
//     /debootstrap/debootstrap --second-stage`. If the chroot already has a
//     populated /var/lib/dpkg/status the foreign+second-stage pair is
//     skipped silently. Bootstrap then writes /etc/apt/sources.list (via
//     Setup) and, if `packages` is non-empty, runs Install for those
//     packages. (Setup + Install land in a follow-up commit.)
//
// All shelled-out commands go through runner.RunCmd so logging and signal
// propagation match the rest of the CLI.
package apt

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"peacock/internal/builder"
	"peacock/internal/runner"
)

// Suite is the Debian release codename targeted by Bootstrap.
type Suite string

const (
	Bookworm Suite = "bookworm"
	Trixie   Suite = "trixie"
	Sid      Suite = "sid"
)

// DefaultSuite is what Bootstrap uses when Config.Suite is empty.
const DefaultSuite = Bookworm

// DefaultMirror is the standard Debian primary archive. Override via
// Config.Mirror or, later, via a --debian-mirror CLI flag.
const DefaultMirror = "http://deb.debian.org/debian"

// DefaultSecurityMirror is the canonical security archive. Not user
// configurable for now — every Debian mirror redirects security to this
// host anyway.
const DefaultSecurityMirror = "http://security.debian.org/debian-security"

// Config is the bootstrap configuration consumed by Bootstrap and
// rendered into /etc/apt/sources.list by GenerateConfigContent.
type Config struct {
	Suite           Suite    // bookworm | trixie | sid; defaults to DefaultSuite
	Arch            string   // dpkg arch: arm64, armhf, amd64
	Mirror          string   // primary mirror; defaults to DefaultMirror
	ExtraComponents []string // extra components beyond "main"; e.g. ["contrib", "non-free"]
}

func (c Config) suite() Suite {
	if c.Suite == "" {
		return DefaultSuite
	}
	return c.Suite
}

func (c Config) mirror() string {
	if c.Mirror == "" {
		return DefaultMirror
	}
	return c.Mirror
}

func (c Config) components() []string {
	if len(c.ExtraComponents) == 0 {
		return []string{"main"}
	}
	out := append([]string{"main"}, c.ExtraComponents...)
	return out
}

// archToDpkg translates a Peacock-canonical architecture string into the
// dpkg architecture name used by debootstrap/apt. Returns "" for unknown
// inputs; callers should treat that as an error. Mirrors the shape of
// pacmanArch() in cmd/peacock/build.go so the alias lives in one obvious
// place per package manager.
func archToDpkg(peacockArch string) string {
	switch peacockArch {
	case "aarch64", "arm64":
		return "arm64"
	case "armv7", "armv7h", "armhf":
		return "armhf"
	case "x86_64", "amd64":
		return "amd64"
	default:
		return ""
	}
}

// ArchToDpkg is the exported wrapper so callers outside the package can
// translate without re-implementing.
func ArchToDpkg(peacockArch string) (string, error) {
	d := archToDpkg(peacockArch)
	if d == "" {
		return "", fmt.Errorf("apt: no dpkg arch mapping for %q", peacockArch)
	}
	return d, nil
}

// GenerateConfigContent renders a canonical /etc/apt/sources.list for the
// given suite + mirror + components. Includes -updates and -security
// stanzas. Multi-line string with a trailing newline.
func GenerateConfigContent(cfg Config) string {
	suite := string(cfg.suite())
	mirror := cfg.mirror()
	comps := strings.Join(cfg.components(), " ")
	var b strings.Builder
	fmt.Fprintf(&b, "deb %s %s %s\n", mirror, suite, comps)
	fmt.Fprintf(&b, "deb %s %s-updates %s\n", mirror, suite, comps)
	// sid has no -security archive; skip the security line for it.
	if cfg.suite() != Sid {
		fmt.Fprintf(&b, "deb %s %s-security %s\n", DefaultSecurityMirror, suite, comps)
	}
	return b.String()
}

// GenerateConfig writes sources.list into <target>/etc/apt/sources.list.
func GenerateConfig(target string, cfg Config) error {
	conf := GenerateConfigContent(cfg)
	confPath := filepath.Join(target, "etc", "apt", "sources.list")
	if err := os.MkdirAll(filepath.Dir(confPath), 0755); err != nil {
		return err
	}
	return os.WriteFile(confPath, []byte(conf), 0644)
}

// qemuStaticBinaryForArch returns the qemu-user-static binary name that
// must be present on the host PATH for foreign-arch second-stage. Empty
// string for native builds where no qemu hop is needed.
func qemuStaticBinaryForArch(dpkgArch string) string {
	host := runtime.GOARCH
	// Native: no qemu needed.
	switch {
	case dpkgArch == "amd64" && host == "amd64":
		return ""
	case dpkgArch == "arm64" && host == "arm64":
		return ""
	case dpkgArch == "armhf" && host == "arm":
		return ""
	}
	switch dpkgArch {
	case "arm64":
		return "qemu-aarch64-static"
	case "armhf":
		return "qemu-arm-static"
	case "amd64":
		return "qemu-x86_64-static"
	default:
		return ""
	}
}

// checkHostPrereqs verifies debootstrap (and a matching qemu-user-static
// for foreign builds) is on PATH. Returns a clear, actionable error if
// not.
func checkHostPrereqs(cfg Config) error {
	if _, err := exec.LookPath("debootstrap"); err != nil {
		return fmt.Errorf("apt bootstrap requires `debootstrap` on PATH.\n" +
			"  Debian/Ubuntu: sudo apt install debootstrap qemu-user-static\n" +
			"  Arch Linux:    sudo pacman -S debootstrap qemu-user-static-binfmt\n" +
			"  Arch (AUR):    yay -S qemu-user-static-bin")
	}
	qemu := qemuStaticBinaryForArch(cfg.Arch)
	if qemu == "" {
		return nil
	}
	if _, err := exec.LookPath(qemu); err != nil {
		return fmt.Errorf("apt bootstrap requires `%s` on PATH for foreign-arch builds.\n"+
			"  Debian/Ubuntu: sudo apt install qemu-user-static\n"+
			"  Arch Linux:    sudo pacman -S qemu-user-static-binfmt\n"+
			"  Arch (AUR):    yay -S qemu-user-static-bin", qemu)
	}
	return nil
}

// alreadyBootstrapped reports whether the chroot at root already looks
// like a finished debootstrap second-stage (i.e. has a non-empty dpkg
// status file). Used to make Bootstrap idempotent.
func alreadyBootstrapped(root string) bool {
	st, err := os.Stat(filepath.Join(root, "var", "lib", "dpkg", "status"))
	if err != nil {
		return false
	}
	return st.Size() > 0
}

// runSudo executes `sudo <args...>` through runner.RunCmd. extraEnv is
// appended to the inherited environment when set.
func runSudo(extraEnv []string, args ...string) error {
	cmd := exec.Command("sudo", args...)
	if len(extraEnv) > 0 {
		cmd.Env = append(os.Environ(), extraEnv...)
	}
	return runner.RunCmd(cmd)
}

// Bootstrap fills <root> with a Debian minbase using debootstrap, copies
// qemu-user-static for foreign-arch second-stage, and runs the second
// stage. Setup + Install are stubbed out here and land in the next
// commit; for now Bootstrap only covers the debootstrap two-stage.
//
// Signature mirrors pacman.Bootstrap(root, packages) so the dispatch in
// cmd/peacock/flavor.go can call either side uniformly. cfg defaults
// match a bookworm-on-arm64 build, which matches the oppo-a16 target.
func Bootstrap(root string, packages []string) error {
	// Build a default Config that targets bookworm on arm64. Real
	// callers (build.go via flavor.go) should switch to
	// BootstrapWithConfig once the device arch is known.
	_ = packages
	cfg := Config{Suite: DefaultSuite, Arch: "arm64", Mirror: DefaultMirror}
	return BootstrapWithConfig(root, cfg, packages)
}

// BootstrapWithConfig is the explicit form. Bootstrap delegates here.
func BootstrapWithConfig(root string, cfg Config, packages []string) error {
	if cfg.Arch == "" {
		return fmt.Errorf("apt.Bootstrap: cfg.Arch is required (use ArchToDpkg)")
	}
	if err := checkHostPrereqs(cfg); err != nil {
		return err
	}

	logf := func(format string, args ...interface{}) {
		fmt.Fprintf(runner.LogWriter(), format, args...)
	}

	if alreadyBootstrapped(root) {
		logf("info: internal/apt: %s already debootstrapped (dpkg status present), skipping foreign+second-stage\n", root)
	} else {
		if err := os.MkdirAll(root, 0755); err != nil {
			return fmt.Errorf("apt.Bootstrap: mkdir %s: %w", root, err)
		}

		suite := string(cfg.suite())
		mirror := cfg.mirror()
		logf("info: internal/apt: debootstrap --foreign --arch=%s %s -> %s\n", cfg.Arch, suite, root)
		if err := runSudo(nil,
			"debootstrap",
			"--foreign",
			"--variant=minbase",
			"--arch="+cfg.Arch,
			suite, root, mirror,
		); err != nil {
			return fmt.Errorf("apt.Bootstrap: debootstrap --foreign: %w", err)
		}

		// Copy qemu-user-static into the chroot for second-stage if we
		// need it. Native builds skip this.
		qemu := qemuStaticBinaryForArch(cfg.Arch)
		if qemu != "" {
			src, err := exec.LookPath(qemu)
			if err != nil {
				return fmt.Errorf("apt.Bootstrap: %s not found on PATH (this should have been caught by checkHostPrereqs): %w", qemu, err)
			}
			dst := filepath.Join(root, "usr", "bin", qemu)
			logf("info: internal/apt: cp %s -> %s\n", src, dst)
			if err := runSudo(nil, "install", "-m", "0755", src, dst); err != nil {
				return fmt.Errorf("apt.Bootstrap: copy %s into chroot: %w", qemu, err)
			}
		}

		logf("info: internal/apt: chroot %s /debootstrap/debootstrap --second-stage\n", root)
		if err := runSudo(nil, "chroot", root, "/debootstrap/debootstrap", "--second-stage"); err != nil {
			return fmt.Errorf("apt.Bootstrap: debootstrap --second-stage: %w", err)
		}
	}

	if err := Setup(root, cfg); err != nil {
		return fmt.Errorf("apt.Bootstrap: setup: %w", err)
	}

	if len(packages) > 0 {
		if err := Install(root, packages); err != nil {
			return fmt.Errorf("apt.Bootstrap: install initial packages: %w", err)
		}
	}
	return nil
}

// Setup writes /etc/apt/sources.list inside root and runs apt-get update.
// Mirrors pacman.Setup's role.
func Setup(root string, cfg Config) error {
	if err := GenerateConfig(root, cfg); err != nil {
		return fmt.Errorf("apt.Setup: write sources.list: %w", err)
	}
	if err := runSudo(
		[]string{"DEBIAN_FRONTEND=noninteractive"},
		"chroot", root, "apt-get", "update",
	); err != nil {
		return fmt.Errorf("apt.Setup: apt-get update: %w", err)
	}
	return nil
}

// Install runs `apt-get install -y --no-install-recommends <packages>`
// inside root. Packages are routed through the debian flavor's alias
// table first so manifests that name `base-devel` (Arch) get rewritten
// to `build-essential` (Debian) on the way in.
func Install(root string, packages []string) error {
	if len(packages) == 0 {
		return nil
	}
	resolved := builder.ResolveBuildDeps(packages, "debian")
	args := []string{
		"chroot", root,
		"apt-get", "install",
		"-y", "--no-install-recommends",
	}
	args = append(args, resolved...)
	if err := runSudo([]string{"DEBIAN_FRONTEND=noninteractive"}, args...); err != nil {
		// Mirror pacman.Install's single-retry policy after a fresh
		// metadata refresh — flaky mirrors are the same problem here.
		fmt.Fprintf(runner.LogWriter(), "apt install failed, refreshing metadata and retrying once...\n")
		_ = runSudo([]string{"DEBIAN_FRONTEND=noninteractive"}, "chroot", root, "apt-get", "update")
		if err2 := runSudo([]string{"DEBIAN_FRONTEND=noninteractive"}, args...); err2 != nil {
			return fmt.Errorf("apt.Install: %w", err2)
		}
	}
	return nil
}
