package main

import (
	"os"
	"strings"
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

// Host-chroot bootstrap + command routing live in internal/pipeline now:
// build.go resolves + validates the flavor via hostChrootFlavor() and
// host.IsSupportedHostChrootFlavor, passes it as RunnerOpts.HostChrootFlavor,
// and the pipeline calls host.EnsureHostChroot + runner.SetExecPrefix
// around the phases. This file keeps only the flag registration and the
// flag/env resolution helper.

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
