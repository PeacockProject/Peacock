package main

// StartBuild binding — kicks off a real `peacock build` and streams its
// output to the React Run.jsx screen via Wails events.
//
// Design note (deviation from plan step 8): the plan instructed to call
// cmd/peacock.RunBuildPipeline directly in a goroutine. That's not
// possible because RunBuildPipeline lives in `package main` (it sits
// next to runBuildSetup, runPackageOrchestration, etc., all of which
// are package-main funcs that read package-main globals like deviceName
// and useQemuFlag). Importing one Go main package from another isn't
// permitted, and lifting the whole phase set + its globals into a
// non-main package is a multi-day refactor we don't want to entangle
// with the GUI shell.
//
// Instead we subprocess the existing `peacock` binary. Its stdout +
// stderr are pumped through io.MultiWriter into both a log file and a
// wailsLogEmitter that fires Wails events ("build:log") for each chunk.
// Cancellation works the same way: context.CancelFunc kills the
// subprocess and unwinds the build. Whether or not the Go code is in
// the same process makes no difference to the user; the React side just
// sees event lines.
//
// When RunBuildPipeline graduates to a real exported package (own plan)
// we replace exec.CommandContext with a goroutine that calls it
// directly. The DTO + event shapes stay the same so the frontend is
// unaffected.

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"

	"peacock/pkg/buildconfig"
)

// BuildRequestDTO is the JSON shape the React Run.jsx form posts. The
// field names mirror buildconfig.BuildPipelineConfig but are JSON tags
// so the frontend can use camelCase / snake_case freely (we set both).
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
// the CLI consumes. Returning buildconfig.BuildPipelineConfig (rather
// than mutating one) keeps the conversion testable in isolation.
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

// StartBuild validates the request, generates a buildID, spawns the
// build goroutine, and returns the ID immediately. The frontend then
// listens for Wails events keyed by ID:
//   - "build:log"    — chunked stdout/stderr (string payload)
//   - "build:error"  — fatal error path (string payload)
//   - "build:done"   — success path (image path string payload)
func (a *App) StartBuild(req BuildRequestDTO) (string, error) {
	cfg := req.toBuildConfig()
	if err := cfg.Validate(); err != nil {
		return "", err
	}

	buildID, err := newBuildID()
	if err != nil {
		return "", err
	}

	ctx, cancel := context.WithCancel(context.Background())
	a.registerBuild(buildID, cancel)

	go a.runBuild(ctx, buildID, cfg)

	return buildID, nil
}

// runBuild owns the lifecycle of one build subprocess. Defers handle
// unregistering the cancel func and closing the log file so a panic in
// the subprocess machinery doesn't leak resources.
func (a *App) runBuild(ctx context.Context, buildID string, cfg buildconfig.BuildPipelineConfig) {
	defer a.unregisterBuild(buildID)

	logPath, logFile, err := openBuildLog(cfg.WorkDir, buildID)
	if err != nil {
		a.emit("build:error", fmt.Sprintf("open log: %v", err))
		return
	}
	defer logFile.Close()

	emitter := &wailsLogEmitter{ctx: a.ctx, event: "build:log"}
	writer := io.MultiWriter(logFile, emitter)

	// Locate the peacock binary. We don't ship it inside the GUI; the
	// expectation is the user has it on PATH or in a sibling dir of
	// the peacock-builder binary. peacockBin returns the first hit.
	bin, err := peacockBin()
	if err != nil {
		a.emit("build:error", err.Error())
		return
	}

	args := buildArgs(cfg)
	cmd := exec.CommandContext(ctx, bin, args...)
	cmd.Stdout = writer
	cmd.Stderr = writer
	if cfg.WorkDir != "" {
		// The peacock CLI resolves peacock-ports relative to cwd, so
		// running with cwd = WorkDir's parent matches the user's
		// `cd Peacock && peacock build …` flow when Peacock is the
		// checkout root. We don't second-guess: cmd inherits the
		// builder's cwd (which is typically the Peacock checkout).
	}

	fmt.Fprintf(writer, "[peacock-builder] starting build %s (log: %s)\n", buildID, logPath)
	fmt.Fprintf(writer, "[peacock-builder] $ %s %s\n", bin, strings.Join(args, " "))

	if err := cmd.Run(); err != nil {
		if ctx.Err() == context.Canceled {
			a.emit("build:error", "cancelled by user")
			return
		}
		a.emit("build:error", err.Error())
		return
	}

	// The peacock CLI writes the image to ~/.local/var/peacock/<dev>.img
	// by default; we surface the conventional path. A future
	// improvement parses the actual path out of stdout.
	imagePath := guessImagePath(cfg)
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
// quietly (shouldn't happen in practice).
type wailsLogEmitter struct {
	ctx   context.Context
	event string
}

// Write satisfies io.Writer. We never fail — log emission is best
// effort, and a transient Wails error must not abort a build.
func (e *wailsLogEmitter) Write(p []byte) (int, error) {
	if e.ctx != nil && len(p) > 0 {
		wailsruntime.EventsEmit(e.ctx, e.event, string(p))
	}
	return len(p), nil
}

// openBuildLog creates ~/.local/var/peacock/logs/build-<id>-<ts>.log
// and returns the path + an open *os.File. WorkDir is used as the
// parent when set; otherwise we fall back to a tmp file so the
// goroutine always has somewhere to write.
func openBuildLog(workDir, buildID string) (string, *os.File, error) {
	parent := workDir
	if parent == "" {
		parent = os.TempDir()
	}
	logsDir := filepath.Join(parent, "logs")
	if err := os.MkdirAll(logsDir, 0o755); err != nil {
		return "", nil, err
	}
	name := fmt.Sprintf("build-%s-%d.log", buildID, time.Now().Unix())
	path := filepath.Join(logsDir, name)
	f, err := os.Create(path)
	if err != nil {
		return "", nil, err
	}
	return path, f, nil
}

// buildArgs flattens a BuildPipelineConfig into the cobra flag list
// the existing `peacock build` command accepts. Names mirror those in
// cmd/peacock/build.go's Flags() block.
func buildArgs(cfg buildconfig.BuildPipelineConfig) []string {
	args := []string{"build", "--device", cfg.Device}
	if cfg.Flavor != "" {
		args = append(args, "--flavor", cfg.Flavor)
	}
	if cfg.InitSystem != "" {
		args = append(args, "--init", cfg.InitSystem)
	}
	if cfg.Desktop != "" {
		args = append(args, "--desktop", cfg.Desktop)
	}
	if cfg.DisplayManager != "" {
		args = append(args, "--display-manager", cfg.DisplayManager)
	}
	for _, e := range cfg.Extras {
		args = append(args, "--extra", e)
	}
	if cfg.UserName != "" {
		args = append(args, "--user", cfg.UserName)
	}
	if cfg.UserPassword != "" {
		args = append(args, "--password", cfg.UserPassword)
	}
	if cfg.ImageSizeMB > 0 {
		args = append(args, "--image-size", strconv.Itoa(cfg.ImageSizeMB))
	}
	if cfg.EmptyRootfs {
		args = append(args, "--empty-rootfs")
	}
	if cfg.UseQemu != "" {
		args = append(args, "--use-qemu", cfg.UseQemu)
	}
	if cfg.CrossCompile != "" {
		args = append(args, "--cross-compile", cfg.CrossCompile)
	}
	return args
}

// peacockBin finds the peacock CLI binary. We check PATH first; then
// a sibling of the running binary; then a sibling of the cwd (the
// `go run ./cmd/peacock` layout). Returning an error here is
// preferable to silently invoking a stale binary.
func peacockBin() (string, error) {
	if p, err := exec.LookPath("peacock"); err == nil {
		return p, nil
	}
	if exe, err := os.Executable(); err == nil {
		candidate := filepath.Join(filepath.Dir(exe), "peacock")
		if _, err := os.Stat(candidate); err == nil {
			return candidate, nil
		}
	}
	if _, err := os.Stat("./peacock"); err == nil {
		return "./peacock", nil
	}
	return "", fmt.Errorf("peacock binary not found on PATH; build it with `go build ./cmd/peacock` and install")
}

// guessImagePath returns the conventional output location used by the
// build pipeline. Parsing the actual path out of build output is a
// follow-up; the current image-assembly phase writes to
// $WORKDIR/<device>.img.
func guessImagePath(cfg buildconfig.BuildPipelineConfig) string {
	if cfg.WorkDir != "" {
		return filepath.Join(cfg.WorkDir, cfg.Device+".img")
	}
	return cfg.Device + ".img"
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
