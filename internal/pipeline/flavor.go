package pipeline

import (
	"context"
	"fmt"

	"peacock/internal/apk"
	"peacock/internal/apt"
	"peacock/internal/config"
	"peacock/internal/pacman"
	"peacock/internal/runner"
)

// BootstrapBaseChroot is the fork point for per-flavor base-distro
// bootstrap. arch hits the existing pacman.Bootstrap path; debian routes
// to the real internal/apt implementation (debootstrap --foreign + qemu
// second-stage + sources.list + apt-get); alpine routes to the real
// internal/apk implementation.
//
// peacockArch is the manifest's Device.Architecture string (aarch64,
// armv7h, x86_64). apt translates it into the dpkg arch (--arch=arm64
// / armhf / amd64) for debootstrap; apk needs it to set --arch on
// `apk add --initdb`. It is unused for the arch flavor today (pacman
// picks up arch via the pacman.conf written separately) but kept on the
// signature so future arch-flavor wiring can pull from the same
// caller-side value.
//
// ctx is accepted but not yet plumbed into the underlying bootstrap
// helpers — those still rely on runner.SetContext for cancellation.
// Keeping it in the signature lets us upgrade later without churning
// the call sites again.
func BootstrapBaseChroot(ctx context.Context, flavor, root, peacockArch string, packages []string) error {
	_ = ctx
	if !config.IsValidFlavor(flavor) {
		return fmt.Errorf("invalid flavor %q (valid: %v)", flavor, config.ValidFlavors)
	}
	switch flavor {
	case "arch":
		// Write the archlinuxarm pacman.conf first so this path is self-contained
		// (the normal build pipeline generates it separately before installing,
		// but a standalone bootstrap — e.g. `peacock bootstrap-flavor` — must do
		// it here, matching the from-scratch alpine/debian cases below).
		if err := pacman.GenerateConfig(root, peacockArch); err != nil {
			return fmt.Errorf("arch pacman.conf: %w", err)
		}
		return pacman.Bootstrap(root, packages)
	case "debian":
		fmt.Fprintf(runner.LogWriter(), "info: bootstrapping debian flavor via internal/apt\n")
		dpkg, err := apt.ArchToDpkg(peacockArch)
		if err != nil {
			return fmt.Errorf("apt bootstrap: %w", err)
		}
		cfg := apt.Config{Suite: apt.DefaultSuite, Arch: dpkg, Mirror: apt.DefaultMirror}
		return apt.BootstrapWithConfig(root, cfg, packages)
	case "alpine":
		fmt.Fprintf(runner.LogWriter(), "info: bootstrapping alpine flavor via internal/apk\n")
		apkArch, err := apk.ArchToApk(peacockArch)
		if err != nil {
			return fmt.Errorf("apk bootstrap: %w", err)
		}
		cfg := apk.Config{Arch: apkArch}
		// Bootstrap (alpine-base + initdb) doesn't write /etc/apk/repositories,
		// so Setup must run before Install can resolve the flavor packages.
		if err := apk.Bootstrap(root, cfg); err != nil {
			return fmt.Errorf("apk bootstrap: %w", err)
		}
		if err := apk.Setup(root, cfg); err != nil {
			return fmt.Errorf("apk setup: %w", err)
		}
		if err := apk.Install(root, packages); err != nil {
			return fmt.Errorf("apk install: %w", err)
		}
		return nil
	default:
		// Unreachable: IsValidFlavor gates entry. Belt-and-braces in
		// case ValidFlavors grows without this switch being updated.
		return fmt.Errorf("unsupported flavor %q", flavor)
	}
}
