package main

// App is the Wails-bound struct. Every exported method becomes
// callable from the JS side via the generated wailsjs/go/main/App.js
// module. The struct itself holds whatever per-session state we need
// across method calls — currently the Wails context (for EventsEmit)
// and a map of in-flight buildID → cancel func so CancelBuild can
// reach into a goroutine started by StartBuild.

import (
	"context"
	"sync"
)

// App holds the Wails runtime context plus per-build cancellation
// handles. The zero value is unusable; construct via NewApp().
type App struct {
	ctx context.Context

	// buildsMu guards builds. We hold it only across map mutation —
	// the goroutine running the build keeps its cancel in a local var
	// too, so a CancelBuild call racing with the goroutine's natural
	// completion is safe.
	buildsMu sync.Mutex
	builds   map[string]context.CancelFunc

	// buildRunMu serializes pipeline EXECUTION. The pipeline + internal/runner
	// keep package-global state (portsRoot, the runner log writer + context),
	// so two builds running at once corrupt each other (wrong ports tree,
	// cross-contaminated logs, swapped cancellation). StartBuild launches each
	// build in its own goroutine; this lock makes them run one-at-a-time
	// (back-to-back), which is the only safe mode until that global state is
	// threaded per-build. Held for the whole pipeline run, so it's separate
	// from buildsMu (which guards only the short map mutations).
	buildRunMu sync.Mutex

	// privilegeErr holds the error (if any) from
	// EnsureBuildPrivileges run at startup. The React side reads it
	// via PrivilegeError() and renders a friendly "we couldn't
	// acquire admin rights" panel when non-empty. We deliberately
	// don't fail-fast at startup — the user may want to inspect
	// other tabs (Host check, device picker) even if builds can't
	// run yet.
	privilegeErr string
}

// NewApp returns a fresh App. The Wails context is filled in by
// startup once the runtime spins up.
func NewApp() *App {
	return &App{
		builds: make(map[string]context.CancelFunc),
	}
}

// startup is registered as options.App.OnStartup. Wails calls it
// after the webview is ready, handing us a context that's valid
// for the lifetime of the window. EventsEmit / EventsOn use it.
//
// We also kick EnsureBuildPrivileges here so the polkit / osascript
// dialog (Linux / macOS) pops at GUI launch rather than blocking the
// first build attempt mid-pipeline. Any error is stashed on the App
// struct and surfaced to the frontend via PrivilegeError(); we don't
// fail startup so the user can still see Host check / device pages.
func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	if err := EnsureBuildPrivileges(ctx); err != nil {
		a.privilegeErr = err.Error()
	}
}

// PrivilegeError returns the message stashed by startup when
// EnsureBuildPrivileges failed, or "" when privileges were acquired
// successfully. Bound for the React side to render a banner / panel.
func (a *App) PrivilegeError() string {
	return a.privilegeErr
}

// PrivilegeMode returns the short description of how privileges are
// being managed on the current platform. Bound for the React side to
// show in the Host check tile (e.g. "linux: sudo credential cache +
// pkexec fallback").
func (a *App) PrivilegeMode() string {
	return SudoPrivilegeMode()
}

// registerBuild stores the cancel func so CancelBuild can find it.
func (a *App) registerBuild(id string, cancel context.CancelFunc) {
	a.buildsMu.Lock()
	defer a.buildsMu.Unlock()
	a.builds[id] = cancel
}

// unregisterBuild drops the cancel func once the build goroutine
// returns. Called from the goroutine's defer.
func (a *App) unregisterBuild(id string) {
	a.buildsMu.Lock()
	defer a.buildsMu.Unlock()
	delete(a.builds, id)
}

// CancelBuild looks up the cancel func for buildID and invokes it.
// Returns true when a build was found and cancelled. The frontend
// can ignore the bool — Wails marshals it to JS as a return value.
func (a *App) CancelBuild(buildID string) bool {
	a.buildsMu.Lock()
	cancel, ok := a.builds[buildID]
	a.buildsMu.Unlock()
	if !ok {
		return false
	}
	cancel()
	return true
}
