package pipeline

// Phase 2 of the build pipeline. Walks the device package manifest's
// dependency list and the resolved userland selections, building any
// dependency that has a local port manifest under peacock-ports/{base,device}
// and falling through to the remote pacman repo for everything else.

import (
	"fmt"
	"path/filepath"
	"strings"

	"peacock/internal/builder"
	"peacock/internal/manifest"
	"peacock/internal/userland"
)

// packageOrchestrationResult collects everything phase 2 produces.
// Downstream phases consume the individual slices/maps.
type packageOrchestrationResult struct {
	pkgs            []string          // pacman package names (local + remote)
	localPackages   []string          // host-filesystem .pkg.tar.gz paths
	depBuildDirs    map[string]string // pkg name -> in-chroot build dir
	depPackagePaths map[string]string // pkg name -> .pkg.tar.gz path
}

// runPackageOrchestration performs phase 2. The error return is fatal —
// caller should print + invoke cleanup before exiting.
func (r *Runner) runPackageOrchestration(
	b *builder.Builder,
	pkg *manifest.Package,
	dev *manifest.Device,
	flavor string,
	initSystem string,
	desktopChoice string,
	displayManagerChoice string,
	extraPackages []string,
	workDir string,
) (*packageOrchestrationResult, error) {
	useQemuFlag := r.opts.UseQemu
	crossCompileFlag := r.opts.CrossCompile
	res := &packageOrchestrationResult{
		pkgs:            []string{},
		depBuildDirs:    make(map[string]string),
		depPackagePaths: make(map[string]string),
	}

	// Dependency Resolution & Pre-Build
	fmt.Println("Resolving dependencies...")
	pkgInList := func(list []string, name string) bool {
		for _, v := range list {
			if v == name {
				return true
			}
		}
		return false
	}

	// Iterate dependencies and decide if local (Build + -U) or remote (-S)
	allDeps := append([]string{}, pkg.Build.Dependencies...)
	if initSystem == "openrc" {
		allDeps = append(allDeps, pkg.Build.DependenciesOpenRC...)
	} else {
		allDeps = append(allDeps, pkg.Build.DependenciesSystemd...)
	}

	buildLocalPackage := func(dep string, depManifest string) error {
		depPkg, err := manifest.LoadPackage(depManifest)
		if err != nil {
			return fmt.Errorf("error loading local dep manifest: %w", err)
		}

		if !depPkg.SupportsFlavor(flavor) {
			fmt.Printf("Skipping %s: not built for flavor %q\n", dep, flavor)
			return nil
		}

		_, depChrootArch, err := resolveBuildOptions(depPkg, dev.Device.Architecture, useQemuFlag, crossCompileFlag)
		if err != nil {
			return fmt.Errorf("error resolving build options for %s: %w", dep, err)
		}
		buildChrootDir := filepath.Join(workDir, "build-chroot", depChrootArch)
		buildDirHint := filepath.Join(buildChrootDir, "build", fmt.Sprintf("%s-%s-%s", depPkg.Package.Name, depPkg.Package.Version, dev.Device.Architecture))

		if artifactPath := FindCachedPackageArtifact(b, depPkg, dev.Device.Architecture); artifactPath != "" {
			fmt.Printf("Using cached package %s at %s\n", dep, artifactPath)
			res.localPackages = append(res.localPackages, artifactPath)
			if !pkgInList(res.pkgs, dep) {
				res.pkgs = append(res.pkgs, dep)
			}
			res.depPackagePaths[depPkg.Package.Name] = artifactPath
			if strings.HasPrefix(depPkg.Package.Name, "linux-") && fileExists(buildDirHint) && kernelArtifactExists(buildDirHint) {
				res.depBuildDirs[depPkg.Package.Name] = buildDirHint
			}
			return nil
		}

		buildDir, artifact, err := BuildPackageInChrootStep(b, depPkg, dev.Device.Architecture, workDir, useQemuFlag, crossCompileFlag)
		if err != nil {
			return fmt.Errorf("error processing dependency %s: %w", dep, err)
		}

		res.depBuildDirs[depPkg.Package.Name] = buildDir
		fmt.Printf("Built and packaged %s at %s\n", dep, artifact)
		res.localPackages = append(res.localPackages, artifact)
		res.depPackagePaths[depPkg.Package.Name] = artifact
		if !pkgInList(res.pkgs, dep) {
			res.pkgs = append(res.pkgs, dep)
		}
		return nil
	}

	for _, dep := range allDeps {
		depManifest, ok := LocalPackageManifestPath(dep)
		if ok {
			fmt.Printf("Found local dependency: %s. Building...\n", dep)
			if err := buildLocalPackage(dep, depManifest); err != nil {
				return nil, err
			}
		} else {
			if !pkgInList(res.pkgs, dep) {
				res.pkgs = append(res.pkgs, dep)
			}
		}
	}

	userlandPkgs, warnings, err := userland.ResolveSelections(desktopChoice, displayManagerChoice, initSystem, extraPackages)
	if err != nil {
		fmt.Println(userland.DescribeChoices())
		return nil, fmt.Errorf("userland selection error: %w", err)
	}
	for _, w := range warnings {
		fmt.Printf("Warning: %s\n", w)
	}
	res.pkgs = append(res.pkgs, userlandPkgs...)

	for _, dep := range userlandPkgs {
		if _, ok := res.depPackagePaths[dep]; ok {
			continue
		}
		depManifest, ok := LocalPackageManifestPath(dep)
		if !ok {
			continue
		}
		fmt.Printf("Found local userland package: %s. Building...\n", dep)
		if err := buildLocalPackage(dep, depManifest); err != nil {
			return nil, err
		}
	}

	return res, nil
}
