package main

import (
	"context"
	"fmt"

	"peacock/internal/apk"
	"peacock/internal/apt"
	"peacock/internal/config"
	"peacock/internal/pacman"
	"peacock/internal/runner"
)

// bootstrapBaseChroot is the fork point for per-flavor base-distro
// bootstrap. arch hits the existing pacman.Bootstrap path; debian and
// alpine route to their stubs which return "flavor not yet implemented"
// so callers fail fast instead of crashing mid-build. Lives in its own
// file so both build.go and build_packages.go can reuse it.
//
// ctx is accepted but not yet plumbed into the underlying bootstrap
// helpers — those still rely on runner.SetContext for cancellation.
// Keeping it in the signature lets us upgrade later without churning
// the call sites again.
func bootstrapBaseChroot(ctx context.Context, flavor, root string, packages []string) error {
	_ = ctx
	if !config.IsValidFlavor(flavor) {
		return fmt.Errorf("invalid flavor %q (valid: %v)", flavor, config.ValidFlavors)
	}
	switch flavor {
	case "arch":
		return pacman.Bootstrap(root, packages)
	case "debian":
		fmt.Fprintf(runner.LogWriter(), "info: bootstrapping debian flavor via internal/apt (stub)\n")
		return apt.Bootstrap(root, packages)
	case "alpine":
		fmt.Fprintf(runner.LogWriter(), "info: bootstrapping alpine flavor via internal/apk (stub)\n")
		return apk.Bootstrap(root, packages)
	default:
		// Unreachable: IsValidFlavor gates entry. Belt-and-braces in
		// case ValidFlavors grows without this switch being updated.
		return fmt.Errorf("unsupported flavor %q", flavor)
	}
}
