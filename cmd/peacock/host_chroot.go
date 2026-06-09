package main

import (
	"fmt"
	"os"
	"strings"

	"peacock/internal/host"
)

// useHostChrootFlag captures the value of `peacock build
// --use-host-chroot <flavor>` (also reads PEACOCK_HOST_CHROOT). The
// flag's intent is documented next to its registration further down.
var useHostChrootFlag string

// hostChrootFlavor returns the requested host-chroot flavor, honoring
// (in this order):
//
//  1. the --use-host-chroot flag value
//  2. the PEACOCK_HOST_CHROOT env var
//
// Returns "" when host-chroot mode is disabled.
func hostChrootFlavor() string {
	if useHostChrootFlag != "" {
		return useHostChrootFlag
	}
	if env := strings.TrimSpace(os.Getenv("PEACOCK_HOST_CHROOT")); env != "" {
		return env
	}
	return ""
}

// ensureHostChrootIfRequested wires --use-host-chroot into
// build.go's early-validation block. The build path calls this before
// it starts touching the host. When host-chroot mode is on, this
// idempotently materializes the rootfs and returns its root path so
// callers can prefix subsequent commands with `chroot <root> ...`.
//
// v0: EnsureHostChroot returns a clear "not yet implemented" error.
// We surface it as a fatal-on-stderr from the build path so the user
// sees exactly what would happen and where it stopped, rather than
// silently falling back to the host.
func ensureHostChrootIfRequested() (rootDir string, ok bool, err error) {
	flavor := hostChrootFlavor()
	if flavor == "" {
		return "", false, nil
	}
	if !host.IsSupportedHostChrootFlavor(flavor) {
		return "", false, fmt.Errorf("--use-host-chroot=%s: unsupported flavor (supported: %v)", flavor, host.SupportedHostChrootFlavors)
	}
	root, err := host.EnsureHostChroot(flavor)
	if err != nil {
		return "", false, err
	}
	return root, true, nil
}

func init() {
	// Register the flag on buildCmd. buildCmd is defined in build.go;
	// landing the registration in this file keeps the host-chroot
	// scaffolding self-contained and avoids merge conflicts with the
	// sibling build.go split.
	buildCmd.Flags().StringVar(
		&useHostChrootFlag,
		"use-host-chroot",
		"",
		"Run the build inside a host chroot (arch|debian|alpine). pmbootstrap-style; v0 scaffolding only — see BACKLOG.md.",
	)
}
