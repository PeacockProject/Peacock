package main

// App is the Wails-bound struct. Mirrors the shape of
// cmd/peacock-builder/app.go (same map-of-cancels pattern) so the React
// frontend bindings stay symmetrical between the two binaries — the
// only difference is "builds" became "installs".

import (
	"context"
	"sync"
)

// App holds the Wails runtime context plus per-install cancellation
// handles. The zero value is unusable; construct via NewApp().
type App struct {
	ctx context.Context

	// installsMu guards installs. Held only across map mutation — the
	// goroutine running an install keeps its cancel in a local var
	// too, so CancelInstall racing with natural completion is safe.
	installsMu sync.Mutex
	installs   map[string]context.CancelFunc
}

// NewApp returns a fresh App. The Wails context is filled in by
// startup once the runtime spins up.
func NewApp() *App {
	return &App{
		installs: make(map[string]context.CancelFunc),
	}
}

// startup is registered as options.App.OnStartup. Wails calls it
// after the webview is ready, handing us a context that's valid for
// the lifetime of the window. EventsEmit / EventsOn use it.
func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
}

// registerInstall stores the cancel func so CancelInstall can find it.
func (a *App) registerInstall(id string, cancel context.CancelFunc) {
	a.installsMu.Lock()
	defer a.installsMu.Unlock()
	a.installs[id] = cancel
}

// unregisterInstall drops the cancel func once the install goroutine
// returns. Called from the goroutine's defer.
func (a *App) unregisterInstall(id string) {
	a.installsMu.Lock()
	defer a.installsMu.Unlock()
	delete(a.installs, id)
}

// CancelInstall looks up the cancel func for installID and invokes
// it. Returns true when an install was found and cancelled.
//
// The installer.RunInstall pipeline checks ctx.Err() at every phase
// boundary so cancellation lands cleanly; in-flight subprocesses get
// SIGTERM via exec.CommandContext under the same ctx.
func (a *App) CancelInstall(installID string) bool {
	a.installsMu.Lock()
	cancel, ok := a.installs[installID]
	a.installsMu.Unlock()
	if !ok {
		return false
	}
	cancel()
	return true
}
