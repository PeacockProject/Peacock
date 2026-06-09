// Package installer drives the PeacockOS install-to-disk pipeline used by the
// peacock-installer Wails app shipped on the live ISO.
//
// IMPORTANT: every function in this package assumes it is running as root on
// a live ISO. The shell-outs (parted, mkfs, rsync, grub-install, chroot,
// useradd, etc.) bind-mount the host's /proc, /sys, /dev into the target
// rootfs and write to raw block devices. There is no sudo wrapping inside
// this package — the peacock-installer binary refuses to start when launched
// as non-root and surfaces a clear error to the GUI before any of these
// helpers ever runs. Unit tests therefore exercise only the pure helpers
// (Config.Validate, DefaultLayout, ListDisks parsing); the destructive paths
// must be exercised on a live-USB VM.
package installer

import (
	"errors"
	"fmt"
	"strings"
)

// Phase is the high-level installer step currently running. The GUI maps
// Phase values to its own progress UI sections; the strings are stable.
type Phase string

const (
	PhasePartitioning  Phase = "partitioning"
	PhaseFormatting    Phase = "formatting"
	PhaseCopySystem    Phase = "copy-system"
	PhaseBootloader    Phase = "bootloader"
	PhaseUserAndConfig Phase = "user-and-config"
	PhaseFinishing     Phase = "finishing"
)

// Progress is one event emitted on the channel passed to RunInstall.
// Percent is 0-100 within the current Phase (not overall). LogLine is
// the raw subprocess output line that triggered this tick — it may be
// empty for phase-boundary "tick" events.
type Progress struct {
	Phase   Phase
	Percent int
	Message string
	LogLine string
}

// UserSpec describes the human account created on the target system.
// Password is plaintext at this layer; the chroot side hashes it via
// chpasswd. Do not log Password.
type UserSpec struct {
	Username  string
	Fullname  string
	Password  string
	Autologin bool
}

// Config is the full input to RunInstall. All fields are required unless
// noted; SourceRoot and BootloaderMode have sensible defaults applied by
// Validate.
type Config struct {
	TargetDiskNode string // e.g. "/dev/sda" or "/dev/mmcblk0"
	PartMode       string // "erase" | "manual" — only "erase" supported in v0

	User     UserSpec
	Hostname string
	Locale   string // e.g. "en_US.UTF-8"
	Keymap   string // e.g. "us"
	Timezone string // e.g. "America/Los_Angeles"

	SourceRoot     string // live rootfs path, default "/run/live"
	BootloaderMode string // "grub" | "extlinux" — autodetected if empty
}

// Result is currently a placeholder for future install-result metadata
// (path to log file, generated UUIDs, etc.). RunInstall returns error only
// today; Result is reserved.
type Result struct {
	TargetDevice string
	RootDevice   string
	BootDevice   string
}

// Validate checks Config for missing or malformed fields and fills in
// defaults for SourceRoot and BootloaderMode.
func (c *Config) Validate() error {
	if c == nil {
		return errors.New("installer: nil config")
	}

	var errs []string

	if c.TargetDiskNode == "" {
		errs = append(errs, "TargetDiskNode is required")
	} else if !strings.HasPrefix(c.TargetDiskNode, "/dev/") {
		errs = append(errs, fmt.Sprintf("TargetDiskNode %q must be a /dev/ path", c.TargetDiskNode))
	}

	switch c.PartMode {
	case "":
		c.PartMode = "erase"
	case "erase":
		// ok
	case "manual":
		errs = append(errs, "PartMode=manual is not supported in v0")
	default:
		errs = append(errs, fmt.Sprintf("PartMode %q is not valid (want erase|manual)", c.PartMode))
	}

	if c.User.Username == "" {
		errs = append(errs, "User.Username is required")
	} else if !validUnixUsername(c.User.Username) {
		errs = append(errs, fmt.Sprintf("User.Username %q is not a valid unix username", c.User.Username))
	}
	if c.User.Password == "" {
		errs = append(errs, "User.Password is required")
	}

	if c.Hostname == "" {
		errs = append(errs, "Hostname is required")
	} else if !validHostname(c.Hostname) {
		errs = append(errs, fmt.Sprintf("Hostname %q is not a valid hostname", c.Hostname))
	}

	if c.Locale == "" {
		errs = append(errs, "Locale is required (e.g. en_US.UTF-8)")
	}
	if c.Keymap == "" {
		errs = append(errs, "Keymap is required (e.g. us)")
	}
	if c.Timezone == "" {
		errs = append(errs, "Timezone is required (e.g. America/Los_Angeles)")
	}

	if c.SourceRoot == "" {
		c.SourceRoot = "/run/live"
	}

	switch c.BootloaderMode {
	case "":
		// leave empty; RunInstall autodetects via /sys/firmware/efi
	case "grub", "extlinux":
		// ok
	default:
		errs = append(errs, fmt.Sprintf("BootloaderMode %q is not valid (want grub|extlinux)", c.BootloaderMode))
	}

	if len(errs) > 0 {
		return fmt.Errorf("installer: invalid config: %s", strings.Join(errs, "; "))
	}
	return nil
}

// validUnixUsername mirrors the POSIX-ish set: starts with [a-z_], remainder
// [a-z0-9_-], length <= 32. Looser than NAME_REGEX on most distros but
// catches the obvious garbage.
func validUnixUsername(s string) bool {
	if len(s) == 0 || len(s) > 32 {
		return false
	}
	for i, r := range s {
		switch {
		case r >= 'a' && r <= 'z':
		case r == '_':
		case i > 0 && r >= '0' && r <= '9':
		case i > 0 && r == '-':
		default:
			return false
		}
	}
	return true
}

// validHostname is RFC 1123-ish: 1-63 chars per label, [a-z0-9-], no
// leading/trailing hyphen. We accept lowercased only — the GUI lowercases
// before handing it in.
func validHostname(s string) bool {
	if len(s) == 0 || len(s) > 253 {
		return false
	}
	for _, label := range strings.Split(s, ".") {
		if len(label) == 0 || len(label) > 63 {
			return false
		}
		if label[0] == '-' || label[len(label)-1] == '-' {
			return false
		}
		for _, r := range label {
			switch {
			case r >= 'a' && r <= 'z':
			case r >= '0' && r <= '9':
			case r == '-':
			default:
				return false
			}
		}
	}
	return true
}
