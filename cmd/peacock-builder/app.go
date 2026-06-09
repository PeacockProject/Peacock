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
func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
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
