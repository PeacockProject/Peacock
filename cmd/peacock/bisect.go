// Bisect — interactive devloop tool for tracking down which port in a
// big build-order is the one that's broken.
//
// UX:
//
//	peacock build-packages --bisect <port-name>
//
// Builds the suspect port's local-manifest dep tree from leaves up,
// captures success/failure per port, and on the first failure drops
// the user into a small interactive prompt with options to enter the
// chroot, tail the build log, retry, skip + continue, or abort. A
// summary table is printed at the end.
//
// The interactive driver is deliberately untested — it's a TTY tool,
// not a library. The dep-tree walker (computeBuildOrder) IS tested
// because it's the only piece with non-trivial logic that doesn't
// require a TTY.
package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"peacock/internal/builder"
	"peacock/internal/chroot"
	"peacock/internal/config"
	"peacock/internal/manifest"
	"peacock/internal/pipeline"
	"peacock/internal/runner"
)

// bisectStatus is the per-port outcome the summary table records.
type bisectStatus string

const (
	bisectOK      bisectStatus = "ok"
	bisectSkipped bisectStatus = "skipped"
	bisectFailed  bisectStatus = "failed"
	bisectPending bisectStatus = "pending"
)

// bisectResult captures a single port's outcome for the summary table.
type bisectResult struct {
	Port    string
	Status  bisectStatus
	LogPath string
}

// computeBuildOrder walks the local-manifest dep tree rooted at
// rootPort and returns a leaves-up build order. Deps absent from the
// manifests map are treated as already-satisfied leaves and dropped
// from the output — the assumption is they're system packages the
// chroot already provides. Cycles return an error.
//
// This is the testable kernel of the bisect command; the interactive
// loop and chroot-drop-in live in runBisect, which is untested.
func computeBuildOrder(rootPort string, manifests map[string]*manifest.Package) ([]string, error) {
	state := map[string]int{} // 0 unseen, 1 visiting, 2 done
	var order []string

	var dfs func(string) error
	dfs = func(name string) error {
		switch state[name] {
		case 1:
			return fmt.Errorf("dependency cycle detected at %q", name)
		case 2:
			return nil
		}
		pkg, ok := manifests[name]
		if !ok {
			// Treat unknown nodes as already-satisfied system packages
			// — they don't appear in the build order. The driver
			// surfaces them at the next layer up via the warning log.
			state[name] = 2
			return nil
		}

		state[name] = 1
		// Local manifest deps to walk: Build.Dependencies + Package.Depends.
		// build_deps (system-package names) are intentionally NOT walked
		// here — they're target-distro names that won't appear as
		// local manifests.
		deps := append([]string{}, pkg.Build.Dependencies...)
		deps = append(deps, pkg.Package.Depends...)
		// Sort the deps so the build order is deterministic regardless
		// of how the caller assembled the map — the test suite asserts
		// on the exact ordering.
		sort.Strings(deps)
		for _, dep := range deps {
			d := normalizeDepName(dep)
			if d == "" || d == name {
				continue
			}
			if err := dfs(d); err != nil {
				return err
			}
		}
		state[name] = 2
		order = append(order, name)
		return nil
	}

	if err := dfs(rootPort); err != nil {
		return nil, err
	}
	return order, nil
}

// loadLocalManifests reads every package.toml referenced (transitively)
// from rootPort and returns the name → *Package map. Unknown deps are
// silently skipped — they're assumed to be system packages.
func loadLocalManifests(rootPort string) (map[string]*manifest.Package, error) {
	manifests := map[string]*manifest.Package{}
	var visit func(string) error
	visit = func(name string) error {
		if _, done := manifests[name]; done {
			return nil
		}
		path, ok := pipeline.LocalPackageManifestPath(name)
		if !ok {
			// Not local; treat as satisfied leaf.
			return nil
		}
		pkg, err := manifest.LoadPackage(path)
		if err != nil {
			return fmt.Errorf("loading %s: %w", name, err)
		}
		manifests[name] = pkg
		// Walk both Build.Dependencies and Package.Depends so the
		// loaded map matches what computeBuildOrder will traverse.
		deps := append([]string{}, pkg.Build.Dependencies...)
		deps = append(deps, pkg.Package.Depends...)
		for _, dep := range deps {
			d := normalizeDepName(dep)
			if d == "" || d == name {
				continue
			}
			if err := visit(d); err != nil {
				return err
			}
		}
		return nil
	}
	if err := visit(rootPort); err != nil {
		return nil, err
	}
	if _, ok := manifests[rootPort]; !ok {
		return nil, fmt.Errorf("local package not found: %s", rootPort)
	}
	return manifests, nil
}

// runBisect is the cobra RunE body. Wired into build-packages via the
// --bisect flag; pulled out into its own function for readability.
func runBisect(rootPort string) error {
	workDir := config.WorkDir()
	if workDir == "" {
		return fmt.Errorf("work directory not set; run 'peacock init' first")
	}

	flavor := strings.TrimSpace(buildPackagesFlavor)
	if flavor == "" {
		flavor = config.Flavor()
	}
	if !config.IsValidFlavor(flavor) {
		return fmt.Errorf("invalid flavor %q (valid: %v)", flavor, config.ValidFlavors)
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

	manifests, err := loadLocalManifests(rootPort)
	if err != nil {
		return err
	}

	order, err := computeBuildOrder(rootPort, manifests)
	if err != nil {
		return err
	}
	fmt.Printf("Bisect build order (%d): %s\n", len(order), strings.Join(order, ", "))

	cacheDir := filepath.Join(workDir, "peacock-cache")
	b, err := builder.NewBuilder(cacheDir)
	if err != nil {
		return fmt.Errorf("failed to initialize builder: %w", err)
	}

	bisectDir := filepath.Join(workDir, "bisect")
	if err := os.MkdirAll(bisectDir, 0o755); err != nil {
		return fmt.Errorf("failed to create bisect dir: %w", err)
	}

	results := make([]bisectResult, 0, len(order))
	stdin := bufio.NewReader(os.Stdin)

	for _, name := range order {
		pkg, ok := manifests[name]
		if !ok {
			// Shouldn't happen — order only contains names that have
			// manifests by construction.
			continue
		}
		logPath := filepath.Join(bisectDir, name+".log")
		fmt.Printf("\n=== bisect: building %s ===\n", name)
		status, err := tryBuild(b, pkg, targetArch, workDir, logPath)
		if err != nil {
			fmt.Printf("FAIL: %s: %v\n", name, err)
			results = append(results, bisectResult{Port: name, Status: bisectFailed, LogPath: logPath})
			choice := promptBisect(stdin, name, workDir, logPath)
			switch choice {
			case bisectChoiceSkip:
				// Replace the last entry's status with "skipped" so
				// the summary table reads correctly.
				results[len(results)-1].Status = bisectSkipped
				fmt.Printf("Skipping %s and continuing.\n", name)
				continue
			case bisectChoiceRetry:
				// Re-attempt the build; if it succeeds, flip the status.
				if status, retryErr := tryBuild(b, pkg, targetArch, workDir, logPath); retryErr == nil {
					results[len(results)-1].Status = bisectOK
					fmt.Printf("Retry succeeded: %s -> %s\n", name, status)
					continue
				} else {
					fmt.Printf("Retry still failing: %v\n", retryErr)
					// Drop user back to the prompt by re-running this
					// port. Simpler: print the summary and abort here.
					printBisectSummary(results)
					return fmt.Errorf("aborted after retry failed for %s", name)
				}
			case bisectChoiceAbort:
				printBisectSummary(results)
				return fmt.Errorf("aborted by user at %s", name)
			default:
				// promptBisect should never return this; treat as abort.
				printBisectSummary(results)
				return fmt.Errorf("aborted at %s (unknown choice)", name)
			}
		}
		fmt.Printf("OK: %s -> %s\n", name, status)
		results = append(results, bisectResult{Port: name, Status: bisectOK, LogPath: logPath})
	}

	printBisectSummary(results)
	return nil
}

// tryBuild runs a single port through buildPackageInChrootStep with
// the runner log redirected to logPath so the interactive "print build
// log" action has something to tail. Returns the artifact path on
// success.
func tryBuild(b *builder.Builder, pkg *manifest.Package, targetArch, workDir, logPath string) (string, error) {
	f, err := os.Create(logPath)
	if err != nil {
		return "", fmt.Errorf("opening bisect log %s: %w", logPath, err)
	}
	defer f.Close()
	old := runner.LogWriter()
	runner.SetLogWriter(io.MultiWriter(old, f))
	defer runner.SetLogWriter(old)
	_, artifact, err := pipeline.BuildPackageInChrootStep(b, pkg, targetArch, workDir, buildPackagesUseQemu, buildPackagesCrossCompile)
	return artifact, err
}

type bisectChoice int

const (
	bisectChoiceUnknown bisectChoice = iota
	bisectChoiceRetry
	bisectChoiceSkip
	bisectChoiceAbort
)

// promptBisect drives the interactive prompt that fires after a port
// fails. Returns the chosen action.
func promptBisect(r *bufio.Reader, port, workDir, logPath string) bisectChoice {
	for {
		fmt.Println()
		fmt.Printf("Bisect prompt — %s failed. Options:\n", port)
		fmt.Println("  [c] enter chroot")
		fmt.Println("  [l] print build log (last 100 lines)")
		fmt.Println("  [r] retry build")
		fmt.Println("  [s] skip + continue")
		fmt.Println("  [a] abort")
		fmt.Print("Choice: ")
		line, err := r.ReadString('\n')
		if err != nil {
			fmt.Printf("Read error (%v); aborting.\n", err)
			return bisectChoiceAbort
		}
		choice := strings.ToLower(strings.TrimSpace(line))
		switch choice {
		case "c", "chroot", "enter":
			if err := bisectEnterChroot(workDir); err != nil {
				fmt.Printf("Error entering chroot: %v\n", err)
			}
		case "l", "log", "print":
			printLastLines(logPath, 100)
		case "r", "retry":
			return bisectChoiceRetry
		case "s", "skip":
			return bisectChoiceSkip
		case "a", "abort", "q", "quit":
			return bisectChoiceAbort
		default:
			fmt.Printf("Unknown choice %q — pick one of c/l/r/s/a.\n", choice)
		}
	}
}

// bisectEnterChroot mounts and drops into the build chroot. Mirrors
// chrootCmd's flow; pulled out so the bisect loop can call into it
// without exec-ing the cobra subcommand.
func bisectEnterChroot(workDir string) error {
	target := filepath.Join(workDir, "chroot")
	if _, err := os.Stat(target); err != nil {
		return fmt.Errorf("chroot dir %s missing: %w", target, err)
	}
	if err := chroot.Mount(target); err != nil {
		return fmt.Errorf("mount: %w", err)
	}
	defer chroot.Unmount(target)
	fmt.Printf("Entering chroot at %s. Type 'exit' to return.\n", target)
	return chroot.Enter(target, nil)
}

// printLastLines tails the last n lines of path to stdout. Survives
// missing files (logs that haven't been written yet) by printing a
// short notice instead of erroring out — the bisect loop should
// recover, not die.
func printLastLines(path string, n int) {
	data, err := os.ReadFile(path)
	if err != nil {
		fmt.Printf("(no log at %s yet: %v)\n", path, err)
		return
	}
	lines := strings.Split(string(data), "\n")
	if len(lines) > n {
		lines = lines[len(lines)-n:]
	}
	fmt.Printf("---- last %d lines of %s ----\n", len(lines), path)
	for _, ln := range lines {
		fmt.Println(ln)
	}
	fmt.Println("---- end ----")
}

// printBisectSummary writes the per-port table. Columns are fixed-
// width so it stays readable in CI logs and in the user's terminal.
func printBisectSummary(results []bisectResult) {
	fmt.Println()
	fmt.Println("Bisect summary:")
	fmt.Println("  PORT                                          STATUS    LOG")
	for _, r := range results {
		fmt.Printf("  %-45s %-9s %s\n", r.Port, string(r.Status), r.LogPath)
	}
}
