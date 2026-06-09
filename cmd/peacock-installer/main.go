// peacock-installer — Wails desktop GUI for installing PeacockOS to a
// target disk from the live ISO.
//
// This is the live-ISO companion to peacock-builder. Where the builder
// runs on a workstation and produces flashable images, the installer
// runs on the booted live medium and writes the system to internal
// storage. Both binaries share the same Wails skeleton shape so the
// React frontend stays largely binary-agnostic.
//
// The frontend is a Vite + React tree that symlinks the shared mock
// components from cmd/peacock-builder/frontend/src/. See "Frontend
// sharing decision" in the plan: keeping one physical copy of the JSX
// avoids drift, and per-binary App.jsx mounts only the install flow.
//
// At startup we refuse to launch when not running as root or not on a
// live ISO (see root_check.go). The GUI must never silently no-op a
// destructive install because of a missing precondition.
package main

import (
	"embed"
	"os"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
)

// assets bundles the Vite production build at compile time. The
// `all:` prefix forces files starting with "." or "_" to be included
// too (Vite emits assets/ with underscore-prefixed names on some
// templates).
//
//go:embed all:frontend/dist
var assets embed.FS

func main() {
	// Refuse to start when not root + not on a live ISO. checkRoot
	// surfaces a native message dialog and returns false; we exit 0
	// because the message has already been shown to the user.
	if !checkPreflight() {
		os.Exit(0)
	}

	app := NewApp()
	err := wails.Run(&options.App{
		Title:  "PeacockInstaller",
		Width:  1280,
		Height: 800,
		AssetServer: &assetserver.Options{
			Assets: assets,
		},
		OnStartup: app.startup,
		Bind: []interface{}{
			app,
		},
	})
	if err != nil {
		panic(err)
	}
}
