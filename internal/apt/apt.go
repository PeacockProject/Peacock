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
// This commit lands the configuration scaffolding only: Suite constants,
// Config struct, arch translation (archToDpkg), and the sources.list
// renderer (GenerateConfigContent / GenerateConfig). Bootstrap, Setup,
// and Install follow in subsequent commits.
package apt

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
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

// Bootstrap / Setup / Install land in subsequent commits. flavor.go
// still calls apt.Bootstrap; keep the symbol around with the old stub
// behavior so this commit doesn't break the build.
func Bootstrap(root string, packages []string) error {
	_ = root
	_ = packages
	return fmt.Errorf("apt.Bootstrap: not yet implemented (lands in follow-up commit)")
}
