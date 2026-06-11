package main

// flash_runner.go — Wails bindings for the chroot-based device flasher.
// The actual fastboot/heimdall work lives in internal/pipeline (flash.go);
// these methods provision the flash chroot once per session and expose
// detection to the React flow. The two-phase flash itself (StartFlash) lands
// on top of these primitives.

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"

	"peacock/internal/builder"
	"peacock/internal/manifest"
	"peacock/internal/pipeline"
	"peacock/internal/runner"
)

var (
	flashMu        sync.Mutex
	flashRootCache string
)

// flashBuilder constructs a Builder rooted at the standard peacock var dir
// (same cache layout the build pipeline uses).
func flashBuilder() (*builder.Builder, error) {
	return builder.NewBuilder(filepath.Join(defaultWorkDir(), "peacock-cache"))
}

// setFlashLog points the shared runner log writer at a live "flash:log"
// emitter for the duration of a flash operation. This matters because the
// build path leaves the global writer pointing at its now-closed log
// MultiWriter; without resetting it, chroot subprocesses (tar, apk) would
// write into a dead pipe and die with SIGPIPE ("broken pipe"). The emitter
// is EventsEmit-based, so it can't broken-pipe.
func (a *App) setFlashLog() {
	if a.ctx == nil {
		return
	}
	// Tee to the GUI process stderr (captured to the launch log file) so flash
	// logs are tailable, plus the frontend event stream.
	runner.SetLogWriter(io.MultiWriter(os.Stderr, &wailsLogEmitter{ctx: a.ctx, event: "flash:log"}))
	runner.SetContext(a.ctx)
}

// ensureFlasher provisions the flash chroot (fastboot + heimdall) exactly once
// per session and returns its root. The first call is slow — it downloads and
// installs the tools — so subsequent detection polls reuse the cached root.
func ensureFlasher() (string, error) {
	flashMu.Lock()
	defer flashMu.Unlock()
	if flashRootCache != "" {
		return flashRootCache, nil
	}
	b, err := flashBuilder()
	if err != nil {
		return "", err
	}
	root, err := pipeline.EnsureFlashChroot(b)
	if err != nil {
		return "", err
	}
	flashRootCache = root
	return root, nil
}

// PrepareFlasher provisions the flash chroot up front (installing the flash
// tools on first run) so device detection is instant afterwards. Returns ""
// on success or an error message for the UI to surface. Safe to call when the
// user reaches the connect step.
func (a *App) PrepareFlasher() string {
	a.setFlashLog()
	if _, err := ensureFlasher(); err != nil {
		return err.Error()
	}
	return ""
}

// DetectFlashDevice probes for the given device in the mode its flash_method
// implies: fastboot serials, or a "download-mode" sentinel for heimdall. An
// empty slice means nothing is connected yet (not an error) — the UI polls.
func (a *App) DetectFlashDevice(deviceCode string) ([]string, error) {
	a.setFlashLog()
	root, err := ensureFlasher()
	if err != nil {
		return nil, err
	}
	method, err := deviceFlashMethod(deviceCode)
	if err != nil {
		return nil, err
	}
	return pipeline.FlashDetect(root, pipeline.FlashTool(method))
}

// deviceFlashMethod reads a device.toml's flash_method (e.g. "fastboot-bootimg",
// "heimdall-bootimg").
func deviceFlashMethod(deviceCode string) (string, error) {
	root, err := portsRoot()
	if err != nil {
		return "", err
	}
	dev, err := manifest.LoadDevice(filepath.Join(root, "device", deviceCode, "device.toml"))
	if err != nil {
		return "", fmt.Errorf("loading device %s: %w", deviceCode, err)
	}
	return dev.Device.FlashMethod, nil
}
