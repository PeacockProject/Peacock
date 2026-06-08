package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"peacock/internal/builder"
	"peacock/internal/config"
	"peacock/internal/manifest"
	"peacock/internal/runner"

	"github.com/spf13/cobra"
)

var (
	buildPackagesDevice       string
	buildPackagesArch         string
	buildPackagesInit         string
	buildPackagesUseQemu      string
	buildPackagesCrossCompile string
	buildPackagesFromFlag     []string
	buildPackagesWithDeps     bool
	buildPackagesRebuild      bool
)

func normalizeDepName(dep string) string {
	dep = strings.TrimSpace(dep)
	if dep == "" {
		return ""
	}
	if i := strings.IndexAny(dep, "<>="); i >= 0 {
		dep = strings.TrimSpace(dep[:i])
	}
	if i := strings.IndexByte(dep, ' '); i >= 0 {
		dep = strings.TrimSpace(dep[:i])
	}
	return dep
}

func expandLocalPackageBuildOrder(roots []string, initSystem string, includeDeps bool) ([]string, error) {
	order := []string{}
	state := map[string]int{} // 0=unseen, 1=visiting, 2=done

	var dfs func(string) error
	dfs = func(name string) error {
		switch state[name] {
		case 1:
			return fmt.Errorf("dependency cycle detected at %s", name)
		case 2:
			return nil
		}

		manifestPath, ok := localPackageManifestPath(name)
		if !ok {
			return nil
		}

		state[name] = 1
		pkg, err := manifest.LoadPackage(manifestPath)
		if err != nil {
			return fmt.Errorf("failed loading %s: %w", name, err)
		}

		if includeDeps {
			deps := append([]string{}, pkg.Build.Dependencies...)
			if strings.EqualFold(initSystem, "openrc") {
				deps = append(deps, pkg.Build.DependenciesOpenRC...)
			} else {
				deps = append(deps, pkg.Build.DependenciesSystemd...)
			}
			deps = append(deps, pkg.Package.Depends...)

			for _, dep := range deps {
				depName := normalizeDepName(dep)
				if depName == "" || depName == name {
					continue
				}
				if err := dfs(depName); err != nil {
					return err
				}
			}
		}

		state[name] = 2
		order = append(order, name)
		return nil
	}

	for _, root := range roots {
		if _, ok := localPackageManifestPath(root); !ok {
			return nil, fmt.Errorf("local package not found: %s", root)
		}
		if err := dfs(root); err != nil {
			return nil, err
		}
	}

	return order, nil
}

func dedupeStrings(in []string) []string {
	seen := make(map[string]struct{}, len(in))
	out := make([]string, 0, len(in))
	for _, v := range in {
		v = strings.TrimSpace(v)
		if v == "" {
			continue
		}
		if _, ok := seen[v]; ok {
			continue
		}
		seen[v] = struct{}{}
		out = append(out, v)
	}
	return out
}

var buildPackagesCmd = &cobra.Command{
	Use:   "build-packages [package ...]",
	Short: "Build specific local peacock packages",
	Long:  "Build only the local packages you specify, without assembling a full disk image.",
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		runner.SetContext(ctx)

		workDir := config.WorkDir()
		if workDir == "" {
			return fmt.Errorf("work directory not set; run 'peacock init' first")
		}

		logDir := filepath.Join(workDir, "logs")
		if err := os.MkdirAll(logDir, 0755); err == nil {
			logPath := filepath.Join(logDir, fmt.Sprintf("build-packages-%s.log", time.Now().Format("20060102-150405")))
			if f, err := os.Create(logPath); err == nil {
				defer f.Close()
				runner.SetLogWriter(f)
				fmt.Printf("Build log: %s\n", logPath)
			}
		}

		targetArch := strings.TrimSpace(buildPackagesArch)
		if targetArch == "" {
			if buildPackagesDevice == "" {
				return fmt.Errorf("specify --device or --arch")
			}
			devPath := filepath.Join("peacock-ports", "device", buildPackagesDevice, "device.toml")
			dev, err := manifest.LoadDevice(devPath)
			if err != nil {
				return fmt.Errorf("failed loading device manifest %s: %w", devPath, err)
			}
			targetArch = dev.Device.Architecture
		}

		requested := append([]string{}, buildPackagesFromFlag...)
		requested = append(requested, args...)
		requested = dedupeStrings(requested)
		if len(requested) == 0 {
			return fmt.Errorf("no packages specified (use args and/or --package)")
		}

		order, err := expandLocalPackageBuildOrder(requested, buildPackagesInit, buildPackagesWithDeps)
		if err != nil {
			return err
		}
		fmt.Printf("Package build order (%d): %s\n", len(order), strings.Join(order, ", "))

		cacheDir := filepath.Join(workDir, "peacock-cache")
		b, err := builder.NewBuilder(cacheDir)
		if err != nil {
			return fmt.Errorf("failed to initialize builder: %w", err)
		}

		artifacts := map[string]string{}

		for _, name := range order {
			manifestPath, ok := localPackageManifestPath(name)
			if !ok {
				return fmt.Errorf("local package not found: %s", name)
			}
			pkg, err := manifest.LoadPackage(manifestPath)
			if err != nil {
				return fmt.Errorf("failed loading package %s: %w", name, err)
			}

			if !buildPackagesRebuild {
				if artifactPath := findCachedPackageArtifact(b, pkg, targetArch); artifactPath != "" {
					fmt.Printf("Using cached package %s at %s\n", name, artifactPath)
					artifacts[name] = artifactPath
					continue
				}
			}

			fmt.Printf("Building %s...\n", name)
			_, artifactPath, err := buildPackageInChrootStep(b, pkg, targetArch, workDir, buildPackagesUseQemu, buildPackagesCrossCompile)
			if err != nil {
				return fmt.Errorf("failed processing %s: %w", name, err)
			}
			artifacts[name] = artifactPath
			fmt.Printf("Built %s -> %s\n", name, artifactPath)
		}

		keys := make([]string, 0, len(artifacts))
		for k := range artifacts {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		fmt.Println("Build complete. Artifacts:")
		for _, k := range keys {
			fmt.Printf("  %s: %s\n", k, artifacts[k])
		}
		return nil
	},
}

func init() {
	rootCmd.AddCommand(buildPackagesCmd)
	buildPackagesCmd.Flags().StringVar(&buildPackagesDevice, "device", "", "Device codename (used to resolve target architecture)")
	buildPackagesCmd.Flags().StringVar(&buildPackagesArch, "arch", "", "Target architecture (e.g. aarch64, armv7h, x86_64)")
	buildPackagesCmd.Flags().StringVar(&buildPackagesInit, "init", "systemd", "Init flavor for dependency expansion: systemd|openrc")
	buildPackagesCmd.Flags().StringSliceVarP(&buildPackagesFromFlag, "package", "p", nil, "Package(s) to build (can be used multiple times)")
	buildPackagesCmd.Flags().BoolVar(&buildPackagesWithDeps, "with-deps", false, "Recursively include local manifest dependencies")
	buildPackagesCmd.Flags().BoolVar(&buildPackagesRebuild, "rebuild", false, "Force rebuild even if matching cached artifact exists")
	buildPackagesCmd.Flags().StringVar(&buildPackagesUseQemu, "use-qemu", "auto", "Use qemu for foreign arch builds: auto|true|false")
	buildPackagesCmd.Flags().StringVar(&buildPackagesCrossCompile, "cross-compile", "", "Cross compiler prefix (e.g. arm-none-eabi-)")
}
