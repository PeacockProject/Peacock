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
	// Root overlays a system-layout feather onto this root (ftr --root),
	// e.g. a staging rootfs the build pipeline assembles.
	Root string
	// DBRoot sandboxes the feather install DB under a rootfs via the
	// FTR_DB_ROOT env var. Empty = ftr's host default.
	DBRoot string
	// AllowUnsigned passes --allow-unsigned. Locally-built artifacts are
	// not signed, so the build pipeline sets this.
	AllowUnsigned bool
	// Sudo wraps the ftr invocation in sudo — needed to write into a
	// root-owned staging rootfs.
	Sudo bool
	// FtrBin pins the ftr binary path. Empty = resolve on PATH / fallback.
	// The build pipeline passes its own (monorepo-aware) resolver result.
	FtrBin string
	// ExtraArgs are appended verbatim after the resolved flags.
	ExtraArgs []string
}

// lookupBinary is the exec.LookPath / os.Stat shim resolveBinary
// delegates to. It's a function var so feather_test.go can swap in
// stubs without touching $PATH or /peacock/bin on the host. Tests
// MUST restore the original value via t.Cleanup.
var lookupBinary = defaultLookupBinary

// defaultLookupBinary is the production behavior: PATH first, fallback
// to /peacock/bin/ftr.
func defaultLookupBinary() (string, bool) {
	if p, err := exec.LookPath("ftr"); err == nil {
		return p, true
	}
	if _, err := os.Stat(fallbackFtrPath); err == nil {
		return fallbackFtrPath, true
	}
	return "", false
}

// resolveBinary looks up ftr on PATH first, then falls back to
// /peacock/bin/ftr. Returns ("", error) when neither is present so
// callers can decide whether to skip the step or fail loudly.
func resolveBinary() (string, error) {
	if p, ok := lookupBinary(); ok {
		return p, nil
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
	if opts.Root != "" {
		args = append(args, "--root", opts.Root)
	}
	if opts.AllowUnsigned {
		args = append(args, "--allow-unsigned")
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

// Install runs `ftr install <pkg>` with the layout-prefix overrides,
// optional --root/--allow-unsigned, an optional sandboxed install DB
// (FTR_DB_ROOT), and optional sudo — everything the build pipeline needs
// to overlay a .feather onto a staging rootfs at its declared layout.
func Install(pkg string, opts InstallOpts) error {
	bin := opts.FtrBin
	if bin == "" {
		var err error
		if bin, err = resolveBinary(); err != nil {
			return err
		}
	}
	args := buildArgs(opts, "install", pkg)

	var cmd *exec.Cmd
	if opts.Sudo {
		sudoArgs := []string{}
		if opts.DBRoot != "" {
			sudoArgs = append(sudoArgs, "env", "FTR_DB_ROOT="+opts.DBRoot)
		}
		sudoArgs = append(sudoArgs, bin)
		sudoArgs = append(sudoArgs, args...)
		cmd = exec.Command("sudo", sudoArgs...)
	} else {
		cmd = exec.Command(bin, args...)
		if opts.DBRoot != "" {
			cmd.Env = append(os.Environ(), "FTR_DB_ROOT="+opts.DBRoot)
		}
	}
	cmd.Stdin = os.Stdin
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	return runner.RunCmd(cmd)
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
