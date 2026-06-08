// Package feather is a phase-3 stub wrapper around the `ftr` binary
// produced by PeacockProject/feather. In phase 3 we only resolve the
// binary on PATH (with a fallback to /peacock/bin/ftr) and shell out;
// phase 4 will land the real overlay-install logic. Callers should
// treat "ftr binary not found" as a soft signal that feather isn't on
// this host yet and skip the step rather than fail the build.
package feather

import (
	"fmt"
	"os"
	"os/exec"
	"strings"

	"peacock/internal/runner"
)

// fallbackFtrPath is where the feather port installs the ftr binary
// when it's not on PATH. Matches FTR_PEACOCK_PREFIX in feather's
// common.h.
const fallbackFtrPath = "/peacock/bin/ftr"

// InstallOpts carries the staging-chroot path overrides that let the
// build pipeline drive ftr against nested prefixes instead of the host
// root.
type InstallOpts struct {
	// PeacockPrefix overrides /peacock when set.
	PeacockPrefix string
	// AppsPrefix overrides /apps when set.
	AppsPrefix string
	// CompatPrefix overrides /compat when set.
	CompatPrefix string
	// DataPrefix overrides /data when set.
	DataPrefix string
	// ExtraArgs are appended verbatim after the resolved flags.
	ExtraArgs []string
}

// resolveBinary looks up ftr on PATH first, then falls back to
// /peacock/bin/ftr. Returns ("", error) when neither is present so
// callers can decide whether to skip the step or fail loudly.
func resolveBinary() (string, error) {
	if p, err := exec.LookPath("ftr"); err == nil {
		return p, nil
	}
	if _, err := os.Stat(fallbackFtrPath); err == nil {
		return fallbackFtrPath, nil
	}
	return "", fmt.Errorf("ftr binary not found — feather not installed (phase 4 will populate)")
}

// buildArgs prepends the prefix-override flags to the user-provided
// subcommand args.
func buildArgs(opts InstallOpts, sub string, tail ...string) []string {
	args := []string{sub}
	if opts.PeacockPrefix != "" {
		args = append(args, "--peacock-prefix", opts.PeacockPrefix)
	}
	if opts.AppsPrefix != "" {
		args = append(args, "--apps-prefix", opts.AppsPrefix)
	}
	if opts.CompatPrefix != "" {
		args = append(args, "--compat-prefix", opts.CompatPrefix)
	}
	if opts.DataPrefix != "" {
		args = append(args, "--data-prefix", opts.DataPrefix)
	}
	args = append(args, opts.ExtraArgs...)
	args = append(args, tail...)
	return args
}

// runFtr is the shared shell-out helper. Stdin is inherited so an
// interactive ftr remove prompt still works.
func runFtr(args []string) error {
	bin, err := resolveBinary()
	if err != nil {
		return err
	}
	cmd := exec.Command(bin, args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	return runner.RunCmd(cmd)
}

// Install runs `ftr install <pkg>` against the staging chroot, with
// prefix overrides honored via flags.
func Install(pkg string, opts InstallOpts) error {
	return runFtr(buildArgs(opts, "install", pkg))
}

// Remove runs `ftr remove <pkg>`. Removal needs the same prefix
// overrides as install so feather can find the per-prefix DB.
func Remove(pkg string) error {
	return runFtr(buildArgs(InstallOpts{}, "remove", pkg))
}

// List shells out to `ftr list` and parses the line-separated output.
func List() ([]string, error) {
	bin, err := resolveBinary()
	if err != nil {
		return nil, err
	}
	out, err := exec.Command(bin, "list").Output()
	if err != nil {
		return nil, err
	}
	var pkgs []string
	for _, line := range strings.Split(string(out), "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		pkgs = append(pkgs, line)
	}
	return pkgs, nil
}

// Files lists the on-disk files owned by pkg via `ftr files <pkg>`.
func Files(pkg string) ([]string, error) {
	bin, err := resolveBinary()
	if err != nil {
		return nil, err
	}
	out, err := exec.Command(bin, "files", pkg).Output()
	if err != nil {
		return nil, err
	}
	var files []string
	for _, line := range strings.Split(string(out), "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		files = append(files, line)
	}
	return files, nil
}

// Available reports whether the ftr binary is present on this host.
// Build pipelines use this to decide whether to invoke feather or skip
// the step silently in phase 3.
func Available() bool {
	_, err := resolveBinary()
	return err == nil
}
