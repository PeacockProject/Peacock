package main

// StartInstall + CancelInstall bindings — kick off a real install via
// the internal/installer package's pipeline and stream progress to the
// React Run.jsx screen via Wails events.
//
// Event names (mirror builder's "build:*" shape):
//   - "install:log"   — chunked log lines (string payload)
//   - "install:phase" — Progress JSON payload (phase + percent + msg)
//   - "install:error" — fatal error path (string payload)
//   - "install:done"  — success path (target-disk node payload)
//
// The Run.jsx component is configurable via the event-name prefix it
// subscribes to (see the prop drilled in api.js); the install side
// emits "install:*" and the builder emits "build:*".
//
// runInstallFn is a package-level function variable that defaults to
// installer.RunInstall. Tests substitute a fake that emits a canned
// Progress sequence without touching real disks.

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"

	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"

	"peacock/internal/installer"
)

// InstallRequestDTO is the JSON shape the React InstallFlow summary
// step posts. Field names are camelCase to match JS conventions; the
// Go side translates into installer.Config.
type InstallRequestDTO struct {
	TargetDiskNode string `json:"targetDiskNode"`
	PartMode       string `json:"partMode"`

	Username  string `json:"username"`
	Fullname  string `json:"fullname"`
	Password  string `json:"password"`
	Autologin bool   `json:"autologin"`

	Hostname string `json:"hostname"`
	Locale   string `json:"locale"`
	Keymap   string `json:"keymap"`
	Timezone string `json:"timezone"`

	SourceRoot     string `json:"sourceRoot"`
	BootloaderMode string `json:"bootloaderMode"`
}

// toConfig translates the DTO into the validated installer.Config.
// TargetDiskNode normalization: the frontend currently posts the
// short kernel name ("sda", "mmcblk0") matching the mock; the
// installer package requires "/dev/" prefixing. We add it if missing.
func (r InstallRequestDTO) toConfig() installer.Config {
	disk := r.TargetDiskNode
	if disk != "" && len(disk) >= 4 && disk[:5] != "/dev/" {
		// "/dev/" is 5 chars; tolerate already-prefixed nodes
		disk = "/dev/" + disk
	}
	return installer.Config{
		TargetDiskNode: disk,
		PartMode:       r.PartMode,
		User: installer.UserSpec{
			Username:  r.Username,
			Fullname:  r.Fullname,
			Password:  r.Password,
			Autologin: r.Autologin,
		},
		Hostname:       r.Hostname,
		Locale:         r.Locale,
		Keymap:         r.Keymap,
		Timezone:       r.Timezone,
		SourceRoot:     r.SourceRoot,
		BootloaderMode: r.BootloaderMode,
	}
}

// runInstallFn is the indirection seam — defaults to the real
// installer.RunInstall, swapped in tests for a fake that doesn't
// touch disks. Signature matches installer.RunInstall.
var runInstallFn = installer.RunInstall

// StartInstall validates the request, allocates an installID, spawns
// a goroutine running the install pipeline, and returns the ID. The
// frontend then listens for "install:*" events keyed by that ID.
func (a *App) StartInstall(req InstallRequestDTO) (string, error) {
	cfg := req.toConfig()
	if err := cfg.Validate(); err != nil {
		return "", err
	}

	installID, err := newInstallID()
	if err != nil {
		return "", err
	}

	ctx, cancel := context.WithCancel(context.Background())
	a.registerInstall(installID, cancel)

	go a.runInstall(ctx, installID, cfg)

	return installID, nil
}

// runInstall is the goroutine body. Owns one install's lifecycle —
// emits events as the underlying pipeline ticks; converts the
// terminal error into either "install:done" or "install:error".
func (a *App) runInstall(ctx context.Context, installID string, cfg installer.Config) {
	defer a.unregisterInstall(installID)

	a.emit("install:log", fmt.Sprintf("[peacock-installer] starting install %s\n", installID))
	a.emit("install:log", fmt.Sprintf("[peacock-installer] target=%s hostname=%s user=%s\n",
		cfg.TargetDiskNode, cfg.Hostname, cfg.User.Username))

	progress := make(chan installer.Progress, 32)
	doneFwd := make(chan struct{})
	go a.forwardProgress(progress, doneFwd)

	err := runInstallFn(ctx, cfg, progress)
	close(progress)
	<-doneFwd

	if err != nil {
		if ctx.Err() == context.Canceled {
			a.emit("install:error", "cancelled by user")
			return
		}
		a.emit("install:error", err.Error())
		return
	}
	a.emit("install:done", cfg.TargetDiskNode)
}

// forwardProgress drains the installer's Progress channel onto Wails
// events. Each Progress becomes two events: a structured
// "install:phase" (for the progress bar + phase pill UI) and an
// "install:log" line (for the terminal scroll buffer). Signals done
// by closing doneFwd.
func (a *App) forwardProgress(progress <-chan installer.Progress, doneFwd chan<- struct{}) {
	defer close(doneFwd)
	for p := range progress {
		a.emitJSON("install:phase", p)
		if p.LogLine != "" {
			a.emit("install:log", "  "+p.LogLine+"\n")
		} else if p.Message != "" {
			a.emit("install:log",
				fmt.Sprintf("[%s] %s\n", p.Phase, p.Message))
		}
	}
}

// emit is a nil-safe wrapper around wails runtime.EventsEmit so unit
// tests that build an App without a running Wails runtime don't panic.
func (a *App) emit(event string, payload string) {
	if a.ctx == nil {
		return
	}
	wailsruntime.EventsEmit(a.ctx, event, payload)
}

// emitJSON sends a non-string payload so the JS handler receives an
// object rather than a string. Wails marshals the value to JSON.
func (a *App) emitJSON(event string, payload interface{}) {
	if a.ctx == nil {
		return
	}
	wailsruntime.EventsEmit(a.ctx, event, payload)
}

// newInstallID returns a short opaque hex ID. 8 bytes of crypto/rand
// → 16 hex chars; collision probability over a session is negligible.
func newInstallID() (string, error) {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(b[:]), nil
}
