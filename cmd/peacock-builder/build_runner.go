package main

// StartBuild binding — kicks off a real build via internal/pipeline
// and streams its output to the React Run.jsx screen via Wails events.
//
// History: this used to subprocess-exec `peacock build` because the
// orchestrator lived in cmd/peacock's main package and Go forbids
// importing a main package. With the pipeline lift in place
// (internal/pipeline) we can call pipeline.Runner.Run directly in a
// goroutine. The frontend event shapes are unchanged — "build:log"
// for chunked stdout, "build:phase" for structured progress when we
// emit it, "build:done" / "build:error" for terminal states.

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"

	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"

	"peacock/internal/pipeline"
	"peacock/internal/runner"
	"peacock/pkg/buildconfig"
)

// BuildRequestDTO is the JSON shape the React Run.jsx form posts. The
// field names mirror buildconfig.BuildPipelineConfig but are JSON tags
// so the frontend can use camelCase / snake_case freely.
type BuildRequestDTO struct {
	Device         string   `json:"device"`
	Flavor         string   `json:"flavor"`
	InitSystem     string   `json:"initSystem"`
	Desktop        string   `json:"desktop"`
	DisplayManager string   `json:"displayManager"`
	Extras         []string `json:"extras"`
	UserName       string   `json:"userName"`
	UserPassword   string   `json:"userPassword"`
	ImageSizeMB    int      `json:"imageSizeMB"`
	EmptyRootfs    bool     `json:"emptyRootfs"`
	UseQemu        string   `json:"useQemu"`
	CrossCompile   string   `json:"crossCompile"`
	WorkDir        string   `json:"workDir"`
	Architecture   string   `json:"architecture"`
}

// toBuildConfig converts the JSON DTO into the validated config struct
// the pipeline consumes.
func (r BuildRequestDTO) toBuildConfig() buildconfig.BuildPipelineConfig {
	return buildconfig.BuildPipelineConfig{
		Device:         r.Device,
		Flavor:         r.Flavor,
		InitSystem:     r.InitSystem,
		Desktop:        r.Desktop,
		DisplayManager: r.DisplayManager,
		Extras:         r.Extras,
		UserName:       r.UserName,
		UserPassword:   r.UserPassword,
		ImageSizeMB:    r.ImageSizeMB,
		EmptyRootfs:    r.EmptyRootfs,
		UseQemu:        r.UseQemu,
		CrossCompile:   r.CrossCompile,
		WorkDir:        r.WorkDir,
		Architecture:   r.Architecture,
	}
}

// toRunnerOpts derives the pipeline.RunnerOpts from the DTO. The
// pipeline package mirrors viper for the rest of cfg, but these four
// (device, qemu, cross-compile, empty-rootfs) live on the runner so
// the phase functions can read them without a side-channel.
func (r BuildRequestDTO) toRunnerOpts() pipeline.RunnerOpts {
	return pipeline.RunnerOpts{
		Device:       r.Device,
		UseQemu:      r.UseQemu,
		CrossCompile: r.CrossCompile,
		EmptyRootfs:  r.EmptyRootfs,
	}
}

// StartBuild validates the request, generates a buildID, spawns the
// build goroutine, and returns the ID immediately. The frontend then
// defaultWorkDir is the standard peacock var dir ($HOME/.local/var/peacock),
// used when the GUI doesn't supply one (it never runs `peacock init`).
// Falls back to "peacock" relative if $HOME can't be resolved.
func defaultWorkDir() string {
	if home, err := os.UserHomeDir(); err == nil {
		return filepath.Join(home, ".local", "var", "peacock")
	}
	return "peacock"
}

// listens for Wails events keyed by ID.
func (a *App) StartBuild(req BuildRequestDTO) (string, error) {
	cfg := req.toBuildConfig()
	// The GUI never runs `peacock init`, so viper's work_dir is unset and
	// the DTO leaves WorkDir empty. Validate() requires it, so default to
	// the standard peacock var dir — the same path the CLI's `init`
	// proposes — when the frontend didn't supply one.
	if cfg.WorkDir == "" {
		cfg.WorkDir = defaultWorkDir()
	}
	if err := cfg.Validate(); err != nil {
		return "", err
	}

	buildID, err := newBuildID()
	if err != nil {
		return "", err
	}

	ctx, cancel := context.WithCancel(context.Background())
	a.registerBuild(buildID, cancel)

	go a.runBuild(ctx, buildID, cfg, req.toRunnerOpts())

	return buildID, nil
}

// runBuild owns the lifecycle of one in-process pipeline build.
// Defers handle unregistering the cancel func + closing the log file
// so a panic in the pipeline machinery doesn't leak resources.
func (a *App) runBuild(ctx context.Context, buildID string, cfg buildconfig.BuildPipelineConfig, opts pipeline.RunnerOpts) {
	defer a.unregisterBuild(buildID)

	logPath, logFile, err := openBuildLog(cfg.WorkDir, buildID)
	if err != nil {
		a.emit("build:error", fmt.Sprintf("open log: %v", err))
		return
	}
	defer logFile.Close()

	// Serialize execution: the pipeline + internal/runner use package-global
	// state (portsRoot, log writer, context) that two concurrent builds would
	// corrupt. Run one build at a time; a second StartBuild's goroutine queues
	// here until the first finishes. (Full per-build threading would remove the
	// need for this lock — tracked in CODE_AUDIT.md.)
	a.buildRunMu.Lock()
	defer a.buildRunMu.Unlock()

	// The pipeline writes shell-subprocess output via runner.LogWriter()
	// and prints higher-level phase messages straight to fmt.Println /
	// fmt.Printf (i.e. stdout). To pump both into the Wails event
	// stream we redirect runner.LogWriter to a MultiWriter that hits
	// both the per-build log file AND a Wails event emitter. Stdout
	// (the higher-level messages) we leave alone — the cobra parent
	// process captures it; in the Wails world it goes to the GUI
	// process's own stdout which is invisible. Acceptable trade-off
	// today; a follow-up adds a structured channel for phase ticks.
	appLog.clear("build:log") // fresh history for this run
	emitter := &wailsLogEmitter{ctx: a.ctx, event: "build:log"}
	writer := io.MultiWriter(logFile, emitter)
	runner.SetLogWriter(writer)
	runner.SetContext(ctx)

	fmt.Fprintf(writer, "[peacock-builder] starting build %s (log: %s)\n", buildID, logPath)
	fmt.Fprintf(writer, "[peacock-builder] device=%s flavor=%s init=%s\n", cfg.Device, cfg.Flavor, cfg.InitSystem)

	// Emit structured phase ticks so the React progress ring + step
	// list advance (the log pane fills from build:log; this drives the
	// percentage + which step is lit). Payload shape matches what
	// useWailsScript parses: {"phase","percent"}.
	opts.Progress = func(phase string, percent int) {
		if a.ctx == nil {
			return
		}
		if b, err := json.Marshal(struct {
			Phase   string `json:"phase"`
			Percent int    `json:"percent"`
		}{phase, percent}); err == nil {
			wailsruntime.EventsEmit(a.ctx, "build:phase", string(b))
		}
	}

	r := pipeline.NewRunner(opts)
	imagePath, err := r.Run(ctx, cfg)
	if err != nil {
		if ctx.Err() == context.Canceled {
			a.emit("build:error", "cancelled by user")
			return
		}
		a.emit("build:error", err.Error())
		return
	}

	a.emit("build:done", imagePath)
}

// emit is a nil-safe wrapper around wails runtime.EventsEmit so unit
// tests that build an App without a running Wails runtime don't panic.
func (a *App) emit(event string, payload string) {
	if a.ctx == nil {
		return
	}
	wailsruntime.EventsEmit(a.ctx, event, payload)
}

// wailsLogEmitter is the io.Writer side of the runner→Wails bridge.
// Each Write call broadcasts a "build:log" event so the React
// Run.jsx terminal scroll buffer can append chunks in real time.
// Writes that arrive after startup but before ctx is set are dropped
// quietly.
type wailsLogEmitter struct {
	ctx   context.Context
	event string
}

// Write satisfies io.Writer. We never fail — log emission is best
// effort, and a transient Wails error must not abort a build.
func (e *wailsLogEmitter) Write(p []byte) (int, error) {
	if len(p) > 0 {
		// Accumulate into the app-scoped buffer FIRST (independent of ctx) so a
		// view mounting late can backfill the full history via App.GetLog.
		appLog.append(e.event, p)
		if e.ctx != nil {
			wailsruntime.EventsEmit(e.ctx, e.event, string(p))
		}
	}
	return len(p), nil
}

// openSessionLog creates <workDir>/logs/<label>-<ts>.log and returns the path +
// an open *os.File. WorkDir is the parent when set; otherwise a tmp dir, so the
// goroutine always has somewhere to write. Used for every build session (system
// build, flashset/recovery) so each persists to its own file under
// ~/.local/var/peacock/logs.
func openSessionLog(workDir, label string) (string, *os.File, error) {
	parent := workDir
	if parent == "" {
		parent = os.TempDir()
	}
	logsDir := filepath.Join(parent, "logs")
	if err := os.MkdirAll(logsDir, 0o755); err != nil {
		return "", nil, err
	}
	name := fmt.Sprintf("%s-%d.log", label, time.Now().Unix())
	path := filepath.Join(logsDir, name)
	f, err := os.Create(path)
	if err != nil {
		return "", nil, err
	}
	return path, f, nil
}

// openBuildLog is openSessionLog for a system build: <workDir>/logs/build-<id>-<ts>.log.
func openBuildLog(workDir, buildID string) (string, *os.File, error) {
	return openSessionLog(workDir, "build-"+buildID)
}

// newBuildID returns a short opaque hex ID. 8 bytes of crypto/rand
// → 16 hex chars; collision probability over a session is negligible.
func newBuildID() (string, error) {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(b[:]), nil
}
