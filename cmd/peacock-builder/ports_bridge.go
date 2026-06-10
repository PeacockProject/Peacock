package main

// PortsStatus / SyncPorts bindings — let the GUI fetch peacock-ports on
// demand. ListDevices stays read-only (ports.Resolve, never clones); the
// first-time-setup screen in the frontend calls SyncPorts when the user
// picks "Build an image" and the tree isn't present yet. Output streams
// to the React clone screen via "ports:log" / "ports:done" / "ports:error"
// events, mirroring the build-event shapes.

import (
	"io"
	"sync"

	"peacock/internal/ports"
	"peacock/internal/runner"
)

// PortsStatusDTO is the read-only presence report the frontend checks
// before entering the build wizard.
type PortsStatusDTO struct {
	Present bool   `json:"present"`
	Root    string `json:"root"`
}

// portsSync guards against two SyncPorts goroutines cloning at once.
var portsSync struct {
	mu    sync.Mutex
	going bool
}

// PortsStatus reports whether a peacock-ports checkout is already
// resolvable. Read-only: it never clones. The frontend uses this to
// decide between showing the clone screen and going straight to the
// device picker.
func (a *App) PortsStatus() PortsStatusDTO {
	root, found := ports.Resolve()
	return PortsStatusDTO{Present: found, Root: root}
}

// SyncPorts ensures a peacock-ports checkout exists, cloning it when
// absent. Returns immediately:
//
//   - If the tree is already present, emits "ports:done" with the root
//     and returns (no clone, no goroutine).
//   - Otherwise spawns a goroutine that streams `git clone` output via
//     "ports:log" and emits "ports:done" (success, payload = root) or
//     "ports:error" (payload = message) when finished.
//
// A second SyncPorts call while a clone is in flight is a no-op — the
// caller just keeps listening to the same event stream.
func (a *App) SyncPorts() PortsStatusDTO {
	if root, found := ports.Resolve(); found {
		a.emit("ports:done", root)
		return PortsStatusDTO{Present: true, Root: root}
	}

	portsSync.mu.Lock()
	if portsSync.going {
		portsSync.mu.Unlock()
		return PortsStatusDTO{Present: false}
	}
	portsSync.going = true
	portsSync.mu.Unlock()

	go a.runPortsSync()
	return PortsStatusDTO{Present: false}
}

// runPortsSync performs the clone, pumping runner output into the
// "ports:log" event so the React clone screen shows live progress.
func (a *App) runPortsSync() {
	defer func() {
		portsSync.mu.Lock()
		portsSync.going = false
		portsSync.mu.Unlock()
	}()

	// Fan runner output (the `git clone` chatter) into the Wails event
	// stream. We don't touch the per-build log file here — this is a
	// one-shot setup step, not a build.
	prev := runner.LogWriter()
	emitter := &wailsLogEmitter{ctx: a.ctx, event: "ports:log"}
	runner.SetLogWriter(io.MultiWriter(prev, emitter))
	defer runner.SetLogWriter(prev)

	root, err := ports.Ensure()
	if err != nil {
		a.emit("ports:error", err.Error())
		return
	}
	a.emit("ports:done", root)
}
