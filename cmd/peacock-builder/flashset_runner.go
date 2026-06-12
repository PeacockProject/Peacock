package main

// StartFlashSet binding — builds a device's bootloader + PRP recovery
// (the "flashable set") as a distinct target from the system image. The
// flash flow triggers this alongside the system build so all three
// artifacts (bootloader, recovery, system) are ready to flash. Streams
// progress via "flashset:log" / "flashset:phase" / "flashset:done" /
// "flashset:error", mirroring the build:* event shapes.

import (
	"encoding/json"
	"io"

	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"

	"peacock/internal/pipeline"
	"peacock/internal/runner"
)

// FlashSetResultDTO is the JSON payload of the flashset:done event: the
// staged artifact paths (empty when the device has no such port).
type FlashSetResultDTO struct {
	Bootloader string `json:"bootloader"`
	Recovery   string `json:"recovery"`
}

// StartFlashSet kicks off the bootloader + PRP build for device in a
// goroutine and returns immediately. Progress + completion arrive via
// flashset:* events. WorkDir defaults to the standard peacock var dir
// (same as StartBuild) since the GUI never runs `peacock init`.
func (a *App) StartFlashSet(device string) {
	go a.runFlashSet(device)
}

func (a *App) runFlashSet(device string) {
	workDir := defaultWorkDir()

	// Fan runner output into flashset:log (don't disturb the build:log
	// writer that a concurrent system build may own — save/restore).
	prev := runner.LogWriter()
	appLog.clear("flashset:log") // fresh history for this run
	emitter := &wailsLogEmitter{ctx: a.ctx, event: "flashset:log"}
	runner.SetLogWriter(io.MultiWriter(prev, emitter))
	defer runner.SetLogWriter(prev)

	progress := func(phase string, percent int) {
		if a.ctx == nil {
			return
		}
		if b, err := json.Marshal(struct {
			Phase   string `json:"phase"`
			Percent int    `json:"percent"`
		}{phase, percent}); err == nil {
			wailsruntime.EventsEmit(a.ctx, "flashset:phase", string(b))
		}
	}

	arts, err := pipeline.BuildFlashSet(device, workDir, "auto", "", progress)
	if err != nil {
		a.emit("flashset:error", err.Error())
		return
	}

	payload, _ := json.Marshal(FlashSetResultDTO{
		Bootloader: arts.Bootloader,
		Recovery:   arts.Recovery,
	})
	if a.ctx != nil {
		wailsruntime.EventsEmit(a.ctx, "flashset:done", string(payload))
	}
}
