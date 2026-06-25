package main

import (
	"context"
	"fmt"
	"path/filepath"
	"strings"

	"peacock/internal/config"
	"peacock/internal/pipeline"
	"peacock/internal/ports"
	"peacock/internal/runner"

	"github.com/spf13/cobra"
)

var (
	bootstrapFlavorFlavor   string
	bootstrapFlavorArch     string
	bootstrapFlavorInit     string
	bootstrapFlavorDevice   string
	bootstrapFlavorOut      string
	bootstrapFlavorPackages []string
)

// bootstrapFlavorCmd bootstraps a base-distro flavor rootfs (the Arch/Alpine/
// Debian userland that runs UNDER /flavors/<flavor> as a Peacock guest) into a
// plain directory, using each distro's native tooling (pacman/apk/debootstrap)
// via pipeline.BootstrapBaseChroot. This is the producer half of the on-device
// "flavor layer": pack the resulting tree into a signed flavor-<name>-base
// feather (see peacock-ports/tools/pack-flavor-base.sh) and publish it to
// genmirror so prp-install can fetch+verify+extract it on-device — no distro
// tooling needed on the phone.
var bootstrapFlavorCmd = &cobra.Command{
	Use:   "bootstrap-flavor",
	Short: "Bootstrap a base-distro flavor rootfs into a directory (producer for the on-device flavor layer)",
	Long: "Bootstrap the Arch/Alpine/Debian base userland that runs under /flavors/<flavor> into --out, " +
		"using the distro's native bootstrap (pacman/apk/debootstrap). Pack + sign + publish the result as a " +
		"flavor-<name>-base feather so prp-install can lay it down on-device.",
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		runner.SetContext(ctx)

		portsRoot, err := ports.Ensure()
		if err != nil {
			return fmt.Errorf("peacock-ports: %w", err)
		}
		pipeline.SetPortsRoot(portsRoot)

		flavor := strings.TrimSpace(bootstrapFlavorFlavor)
		if flavor == "" {
			return fmt.Errorf("--flavor is required (arch|alpine|debian)")
		}
		if !config.IsValidFlavor(flavor) {
			return fmt.Errorf("invalid flavor %q (valid: %v)", flavor, config.ValidFlavors)
		}
		arch := strings.TrimSpace(bootstrapFlavorArch)
		if arch == "" {
			return fmt.Errorf("--arch is required (aarch64|armv7h|x86_64)")
		}
		out := strings.TrimSpace(bootstrapFlavorOut)
		if out == "" {
			return fmt.Errorf("--out is required (target directory for the bootstrapped rootfs)")
		}
		out, err = filepath.Abs(out)
		if err != nil {
			return fmt.Errorf("resolving --out: %w", err)
		}

		pkgs, err := resolveFlavorBasePackages()
		if err != nil {
			return err
		}
		runner.Logf("Bootstrapping %s base for %s into %s (%d packages)\n", flavor, arch, out, len(pkgs))

		if err := pipeline.BootstrapBaseChroot(ctx, flavor, out, arch, pkgs); err != nil {
			return fmt.Errorf("bootstrap %s: %w", flavor, err)
		}
		// stdout (not the log) carries the result path for scripts to capture.
		fmt.Println(out)
		return nil
	},
}

// resolveFlavorBasePackages picks the package set for the flavor base — the
// PURE distro userland that runs under /flavors/<flavor>. An explicit --packages
// list wins; otherwise a per-flavor minimal base group.
//
// NOTE: this deliberately does NOT pull the device port's dependency lists.
// Those conflate distro packages (glibc/bash/coreutils) with peacock-built
// packages (the kernel, firmware, udev-openrc) that the PEACOCK BASE installs
// via feather — feeding them to pacman/apk yields "target not found". The
// kernel/firmware/init live in the base layer; the flavor only needs its distro.
func resolveFlavorBasePackages() ([]string, error) {
	if len(bootstrapFlavorPackages) > 0 {
		return dedupeNonEmpty(bootstrapFlavorPackages), nil
	}
	switch bootstrapFlavorFlavor {
	case "arch":
		// `base` is archlinuxarm's base group — a complete minimal userland.
		return []string{"base"}, nil
	case "alpine":
		return []string{"alpine-base"}, nil
	case "debian":
		// debootstrap installs its own base set; no extra packages needed.
		return nil, nil
	default:
		return []string{"base"}, nil
	}
}

func dedupeNonEmpty(in []string) []string {
	seen := map[string]bool{}
	out := []string{}
	for _, s := range in {
		s = strings.TrimSpace(s)
		if s == "" || seen[s] {
			continue
		}
		seen[s] = true
		out = append(out, s)
	}
	return out
}

func init() {
	bootstrapFlavorCmd.Flags().StringVar(&bootstrapFlavorFlavor, "flavor", "", "base distro flavor (arch|alpine|debian)")
	bootstrapFlavorCmd.Flags().StringVar(&bootstrapFlavorArch, "arch", "", "target architecture (aarch64|armv7h|x86_64)")
	bootstrapFlavorCmd.Flags().StringVar(&bootstrapFlavorInit, "init", "openrc", "init system (openrc|systemd) — selects the device dependency list")
	bootstrapFlavorCmd.Flags().StringVar(&bootstrapFlavorDevice, "device", "", "device codename to pull the base package set from (e.g. xiaomi-daisy)")
	bootstrapFlavorCmd.Flags().StringVar(&bootstrapFlavorOut, "out", "", "output directory for the bootstrapped rootfs")
	bootstrapFlavorCmd.Flags().StringSliceVar(&bootstrapFlavorPackages, "packages", nil, "explicit base package set (comma-separated); overrides --device")
	rootCmd.AddCommand(bootstrapFlavorCmd)
}
