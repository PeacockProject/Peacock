package pipeline

// P6 — single-source the builder. The first-boot OOBE configures a flavor by running its
// configure.toml through peacock-oobe --apply. This hook lets the BUILDER configure a flavor the
// same way at build time — the same blueprint, the same engine — so a built image and a fresh OOBE
// produce an identically-configured flavor, and flavor config lives in exactly one place
// (configure.toml) instead of being duplicated in rootfs.go's chroot heredocs.
//
// Opt-in for now (PEACOCK_BLUEPRINT_CONFIG=1) and ADDITIVE to the existing heredocs, so the default
// build is byte-for-byte unchanged. Migration is per-heredoc: move a block into a configure.toml
// step script, confirm the flagged build matches the default on daisy + qemu-x86_64, then delete
// the Go block. Once every block is migrated, the flag becomes the default and the heredocs go away.

import (
	"os"
	"path/filepath"

	"peacock/internal/config"
	"peacock/internal/runner"
)

// applyConfigureBlueprint runs the flavor's configure.toml against the freshly-populated rootfs via
// peacock-oobe --apply, mapping the build's choices to answers (password as a --secret, never
// written). No-op unless PEACOCK_BLUEPRINT_CONFIG=1. Needs a host-arch peacock-oobe on PATH (or
// $PEACOCK_OOBE_BIN); the foreign-arch chroot inside run_in_target rides the build chroot's
// qemu-user binfmt.
func (r *Runner) applyConfigureBlueprint(rootfsPath, desktop, dm, user, pass string) error {
	if os.Getenv("PEACOCK_BLUEPRINT_CONFIG") != "1" {
		return nil // disabled: rootfs.go's heredocs remain the source of truth
	}
	flavor := config.Flavor()
	if flavor == "" {
		flavor = "arch"
	}
	blueprintDir := filepath.Join(portsRoot, "blueprints", flavor)
	if !fileExistsFile(filepath.Join(blueprintDir, "configure.toml")) {
		runner.Logf("blueprint config: no configure.toml for %q; skipping\n", flavor)
		return nil
	}
	bin := os.Getenv("PEACOCK_OOBE_BIN")
	if bin == "" {
		bin = "peacock-oobe"
	}
	args := []string{
		bin, "--apply", "--kind", "oobe",
		"--root", rootfsPath,
		"--local", blueprintDir,
	}
	if user != "" {
		args = append(args, "--set", "user="+user)
	}
	if desktop != "" {
		args = append(args, "--set", "desktop="+desktop)
	}
	if dm != "" {
		args = append(args, "--set", "dm="+dm)
	}
	if pass != "" {
		args = append(args, "--secret", "pass="+pass)
	}
	runner.Logf("blueprint config: applying %s/configure.toml to the rootfs (single-source)\n", blueprintDir)
	// sudo, mirroring the heredoc execCommand calls — writing into the rootfs + chrooting need root.
	return execCommand("sudo", args...)
}
