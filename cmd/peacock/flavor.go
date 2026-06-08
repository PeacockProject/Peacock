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
// bootstrap. arch hits the existing pacman.Bootstrap path; alpine
// routes to the real internal/apk implementation. debian still hits
// the apt stub today; that parallel work lands separately.
//
// peacockArch is the manifest's Device.Architecture string (aarch64,
// armv7h, x86_64). apk needs it to set --arch on `apk add --initdb`.
// It is unused for the arch flavor today (pacman picks up arch via the
// pacman.conf written separately) but kept on the signature so the
// debian / alpine branches can read it without churning the
// build.go / build_packages.go call sites again.
//
// ctx is accepted but not yet plumbed into the underlying bootstrap
// helpers — those still rely on runner.SetContext for cancellation.
// Keeping it in the signature lets us upgrade later without churning
// the call sites again.
func bootstrapBaseChroot(ctx context.Context, flavor, root, peacockArch string, packages []string) error {
	_ = ctx
	_ = peacockArch // consumed by the alpine branch; debian / arch ignore for now
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
		fmt.Fprintf(runner.LogWriter(), "info: bootstrapping alpine flavor via internal/apk\n")
		apkArch, err := apk.ArchToApk(peacockArch)
		if err != nil {
			return fmt.Errorf("apk bootstrap: %w", err)
		}
		// packages plumbed in on a follow-up commit (Setup + Install).
		_ = packages
		return apk.Bootstrap(root, apk.Config{Arch: apkArch})
	default:
		// Unreachable: IsValidFlavor gates entry. Belt-and-braces in
		// case ValidFlavors grows without this switch being updated.
		return fmt.Errorf("unsupported flavor %q", flavor)
	}
}
