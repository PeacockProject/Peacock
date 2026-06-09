// peacock-builder — Wails desktop GUI for the Peacock CLI.
//
// This is the Wails v2 host process. It embeds the Vite-bundled React
// frontend under frontend/dist/ via go:embed, wires the App struct that
// holds the bound Go methods (ListDevices, RunDoctor, StartBuild,
// CancelBuild), and hands everything to wails.Run which spins up the
// webkit2gtk (Linux) / WKWebView (macOS) / WebView2 (Windows) window.
//
// We do NOT call `wails init` to scaffold the project: the frontend tree
// landed in phase 1 from the maintainer's mock-up, with its own
// vite.config.js + package.json, and a wails-init scaffold would clobber
// that. Hand-authoring the four Wails-specific files (this main.go,
// app.go, wails.json, .gitignore) is enough — Wails detects the project
// shape from wails.json's frontend:* keys.
package main

import (
	"embed"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
)

// assets bundles the Vite production build at compile time. The
// `all:` prefix forces files starting with "." or "_" (Vite emits
// _astro/, etc. on some templates) to be included too.
//
//go:embed all:frontend/dist
var assets embed.FS

func main() {
	app := NewApp()
	err := wails.Run(&options.App{
		Title:  "PeacockBuilder",
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
