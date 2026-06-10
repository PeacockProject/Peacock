// Package pipeline lifts the Peacock build orchestration out of the
// cmd/peacock main package so non-cobra callers (the Wails GUI under
// cmd/peacock-builder, integration tests, future automation tooling)
// can drive a build in-process instead of subprocess-execing the CLI.
//
// The cobra `peacock build` command became a thin wrapper that builds
// a RunnerOpts + BuildPipelineConfig from viper / pflag state and
// hands them to Runner.Run. The Wails GUI does the same from form
// state. Both paths funnel through the same code, so they stay in
// lockstep.
//
// Layout:
//
//	pipeline.go    — Runner, RunnerOpts, NewRunner, (*Runner).Run
//	cleanup.go     — Cleanup struct that tracks mountpoints / loops
//	helpers.go     — package-level helpers (also consumed by cmd/peacock
//	                 bisect + build-packages subcommands)
//	setup.go       — phase 1 (device + flavor + Builder bootstrap)
//	packages.go    — phase 2 (dependency walk + chroot builds)
//	initramfs.go   — phase 3 (busybox / splash / mkinitfs)
//	rootfs.go      — phase 4 (kernel + image-build chroot + rootfs)
//	image.go       — phase 5 (boot.img + disk image)
package pipeline

import (
	"context"
	"fmt"

	"github.com/spf13/viper"

	"peacock/internal/config"
	"peacock/internal/host"
	"peacock/internal/ports"
	"peacock/internal/runner"
	"peacock/pkg/buildconfig"
)

// portsRoot is the resolved peacock-ports tree for the current build. It
// is set once by Run (via ports.Ensure) before any phase executes; the
// phase code joins manifest paths against it instead of the bare
// "peacock-ports" string. Defaults to "peacock-ports" so any direct phase
// call in a test that skips Run still hits the dev-layout path.
var portsRoot = "peacock-ports"

// SetPortsRoot overrides the package-level ports root used by the helper
// functions exported to cmd/peacock (LocalPackageManifestPath et al).
// Run sets this itself via ports.Ensure; the bisect / build-packages
// subcommands, which invoke those helpers without going through Run,
// prime it after their own ports.Ensure so the shared lookup code sees
// the resolved (possibly auto-cloned) tree.
func SetPortsRoot(root string) {
	if root != "" {
		portsRoot = root
	}
}

// RunnerOpts carries the cobra-flag-shaped knobs that used to live as
// package-level globals on cmd/peacock (deviceName, useQemuFlag,
// crossCompileFlag, emptyRootfsFlag). Lifting them onto a struct lets
// the Wails GUI run multiple builds back-to-back without worrying
// about cross-build state bleed, and makes the pipeline package
// testable.
type RunnerOpts struct {
	// Device is the target device codename (e.g. "oppo-a16"). Required.
	Device string

	// UseQemu mirrors --use-qemu. One of "auto" (default), "true",
	// "false". Empty string is treated as "auto" inside resolveBuildOptions.
	UseQemu string

	// CrossCompile mirrors --cross-compile. Optional toolchain prefix
	// (e.g. "arm-none-eabi-"). Empty defers to per-port manifest values.
	CrossCompile string

	// EmptyRootfs mirrors --empty-rootfs. When true, skip rootfs
	// package install / user creation / desktop wiring and produce a
	// small debug image with an empty labeled root partition.
	EmptyRootfs bool

	// HostChrootFlavor mirrors --use-host-chroot. Empty (the default)
	// means host-chroot mode is off and the build shells out directly on
	// the host, byte-identical to legacy behavior. When set to a
	// supported flavor (arch|debian|alpine), Run bootstraps the host
	// chroot via host.EnsureHostChroot and routes every runner shell-out
	// through `sudo chroot <root>` for the duration of the build.
	HostChrootFlavor string

	// Progress, when set, is called at each phase boundary with a
	// human-facing phase label and a rough completion percent (0-100).
	// The GUI wires this to emit "build:phase" Wails events so its
	// progress ticker advances; the CLI leaves it nil. Labels match the
	// frontend's BUILD_PHASES so the matching step lights up.
	Progress func(phase string, percent int)
}

// Runner drives a single build pipeline end-to-end. Construct via
// NewRunner; the zero value is unusable (Device validation would
// trip in Run). Runners are stateless across calls — re-running a
// Runner runs a fresh pipeline.
type Runner struct {
	opts RunnerOpts

	// setupFn overrides the phase-1 implementation. nil (the default)
	// means (*Runner).runBuildSetup. It exists purely as a test seam so
	// pipeline_test.go can exercise Run's control flow (validation,
	// config push, context cancellation) without bootstrapping a real
	// device + chroot.
	setupFn func(ctx context.Context, workDir string) (*buildSetup, error)
}

// NewRunner wraps RunnerOpts into a Runner. We keep the constructor
// trivial today so the GUI's only knob is RunnerOpts; future per-runner
// state (e.g. a context-scoped logger handle) can land here without
// churning callers.
func NewRunner(opts RunnerOpts) *Runner {
	if opts.UseQemu == "" {
		opts.UseQemu = "auto"
	}
	return &Runner{opts: opts}
}

// Opts returns the runner's resolved options. Exposed so callers
// who only have a *Runner (e.g. the cobra cleanup-on-signal path)
// can introspect the device name without threading it separately.
func (r *Runner) Opts() RunnerOpts {
	return r.opts
}

// progress reports a phase boundary to the optional Progress callback.
// No-op when unset (the CLI), so build behavior is unchanged there.
func (r *Runner) progress(phase string, percent int) {
	if r.opts.Progress != nil {
		r.opts.Progress(phase, percent)
	}
}

// Run drives the five phase functions in order using cfg, and returns
// the path of the produced disk image. Errors are wrapped with the
// phase name so a GUI can surface a meaningful failure tag.
//
// It mutates viper to keep parity with the legacy cobra path:
// runBuildSetup et al read viper for flavor / init-system / desktop /
// display-manager / etc, and the Wails GUI doesn't otherwise touch
// viper. Push-then-read keeps both call sites identical.
//
// Sudo and runner-context wiring stay the caller's responsibility
// (the cobra Run sets them; the Wails caller sets its own
// MultiWriter + context). Run itself never spawns its own context.
func (r *Runner) Run(ctx context.Context, cfg buildconfig.BuildPipelineConfig) (string, error) {
	if err := cfg.Validate(); err != nil {
		return "", err
	}

	// Sync the runner opts with viper so the existing phase code (which
	// reads viper directly via internal/config accessors) sees the
	// caller's intent. This is a temporary belt-and-braces — phase
	// functions read from the Runner's own opts where possible.
	pushConfig(cfg)

	// Resolve (auto-cloning if necessary) the peacock-ports tree before
	// any phase reads a manifest. Done before the host-chroot exec prefix
	// is installed so the git clone runs on the host, not inside the
	// chroot. portsRoot is read by every phase's manifest-path joins.
	root, err := ports.Ensure()
	if err != nil {
		return "", fmt.Errorf("peacock-ports: %w", err)
	}
	SetPortsRoot(root)

	// Push RunnerOpts from cfg fields where the GUI supplied them.
	if cfg.UseQemu != "" {
		r.opts.UseQemu = cfg.UseQemu
	}
	if cfg.CrossCompile != "" {
		r.opts.CrossCompile = cfg.CrossCompile
	}
	if r.opts.Device == "" {
		r.opts.Device = cfg.Device
	}
	r.opts.EmptyRootfs = cfg.EmptyRootfs

	// Host-chroot mode (pmbootstrap-style). When a flavor is set, ensure
	// the host chroot exists (bootstrap on first run — can take minutes)
	// and prefix every runner shell-out with `sudo chroot <root>` for the
	// rest of this build. The prefix is cleared on return so it never
	// leaks past Run into a subsequent (non-chroot) build on the same
	// process — important for the Wails GUI running builds back-to-back.
	if r.opts.HostChrootFlavor != "" {
		runner.Logf("Ensuring host chroot for flavor %q (first run downloads + extracts a rootfs; this can take several minutes)...\n", r.opts.HostChrootFlavor)
		root, err := host.EnsureHostChroot(r.opts.HostChrootFlavor)
		if err != nil {
			return "", fmt.Errorf("host chroot: %w", err)
		}
		runner.Logf("Routing build through host chroot: sudo chroot %s\n", root)
		runner.SetExecPrefix([]string{"sudo", "chroot", root})
		defer runner.ClearExecPrefix()
	}

	workDir := cfg.WorkDir

	r.progress("Resolving deps", 4)
	runSetup := r.runBuildSetup
	if r.setupFn != nil {
		runSetup = r.setupFn
	}
	setup, err := runSetup(ctx, workDir)
	if err != nil {
		return "", fmt.Errorf("build setup: %w", err)
	}

	r.progress("Building kernel", 18)
	pkgRes, err := r.runPackageOrchestration(
		setup.b, setup.pkg, setup.dev,
		setup.flavor, setup.initSystem, setup.desktopChoice, setup.displayManagerChoice,
		setup.extraPackages, workDir,
	)
	if err != nil {
		return "", fmt.Errorf("package orchestration: %w", err)
	}

	r.progress("Building busybox", 34)
	initramfsPath, err := r.runInitramfsPhase(
		setup.b, setup.pkg, setup.dev,
		pkgRes.depBuildDirs, pkgRes.depPackagePaths,
		setup.initSystem, workDir,
	)
	if err != nil {
		return "", fmt.Errorf("initramfs: %w", err)
	}

	// Cleanup tracker; phase 4 mutates it (image-build chroot mount). We
	// own the lifecycle here so a GUI caller doesn't have to thread a
	// main-package type through its API surface; on any error path below
	// we Run() it before returning so loops/mounts get torn down.
	cleanup := &Cleanup{workDir: workDir}
	defer cleanup.Run()

	r.progress("Rootfs", 60)
	rootfsRes, err := r.runRootfsPhase(
		setup.b, setup.pkg, setup.dev,
		pkgRes.depBuildDirs, pkgRes.depPackagePaths,
		pkgRes.pkgs, pkgRes.localPackages, setup.cacheDir,
		setup.initSystem, setup.desktopChoice, setup.displayManagerChoice,
		setup.userName, setup.userPassword, setup.emptyRootfs,
		initramfsPath, workDir, cleanup,
	)
	if err != nil {
		return "", fmt.Errorf("rootfs: %w", err)
	}

	r.progress("Disk image", 92)
	imagePath, err := r.runImageAssemblyPhase(
		setup.b, setup.dev,
		rootfsRes.imageChrootRoot, rootfsRes.rootfsPath, rootfsRes.kernelBuildDir,
		initramfsPath, setup.emptyRootfs, workDir,
	)
	if err != nil {
		return "", fmt.Errorf("image assembly: %w", err)
	}

	return imagePath, nil
}

// pushConfig mirrors cfg into viper so the phase code (which reads
// config via internal/config's viper-backed accessors) sees the
// caller's intent. Extracted from Run so the viper plumbing is
// testable without bootstrapping phase 1; behavior is identical.
//
// An empty Flavor defaults to "arch" (parity with config.Flavor()),
// and an empty WorkDir leaves the persisted key untouched so a GUI
// run can't clobber the `peacock init` value with "".
func pushConfig(cfg buildconfig.BuildPipelineConfig) {
	flavor := cfg.Flavor
	if flavor == "" {
		flavor = "arch"
	}
	viper.Set(config.KeyFlavor, flavor)
	viper.Set(config.KeyInitSystem, cfg.InitSystem)
	viper.Set(config.KeyDesktop, cfg.Desktop)
	viper.Set(config.KeyDisplayManager, cfg.DisplayManager)
	viper.Set(config.KeyExtraPackages, cfg.Extras)
	viper.Set(config.KeyUserName, cfg.UserName)
	viper.Set(config.KeyUserPassword, cfg.UserPassword)
	viper.Set(config.KeyImageSizeMB, cfg.ImageSizeMB)
	viper.Set(config.KeyEmptyRootfs, cfg.EmptyRootfs)
	if cfg.WorkDir != "" {
		viper.Set(config.KeyWorkDir, cfg.WorkDir)
	}
}
