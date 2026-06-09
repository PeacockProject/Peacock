package pipeline

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"peacock/internal/chroot"
	"peacock/internal/image"
)

// Cleanup tracks the mountpoints and loop devices a single Run()
// acquires so a deferred Run() (or a signal-handler-driven one) can
// release them in reverse order. Previously this lived on cmd/peacock
// as buildCleanup; lifting it into pipeline lets non-cobra callers
// (the Wails GUI) reuse the same teardown logic without having to
// thread a main-package type through their API surface.
//
// The fields are written from phase 4 (image-build chroot) and
// phase 5 (loop device + mountpoints) as the pipeline progresses;
// Run() consults them all and skips anything still empty.
type Cleanup struct {
	loopDev     string
	installDir  string
	bootDir     string
	workDir     string
	imageChroot string
}

// NewCleanup constructs an empty Cleanup scoped to workDir. The cobra
// signal-handler path needs to create one outside of a running
// pipeline so it can be deferred immediately — the workDir is the only
// field required at that point.
func NewCleanup(workDir string) *Cleanup {
	return &Cleanup{workDir: workDir}
}

// Run executes the cleanup in dependency-reverse order. Each step is
// best-effort: failures get printed but don't short-circuit the
// remaining steps (otherwise a stuck mount could trap the loop device).
// Safe to call multiple times; cleared fields turn into no-ops.
func (c *Cleanup) Run() {
	cleanWork := filepath.Clean(c.workDir)
	if c.imageChroot != "" {
		workMount := filepath.Join(c.imageChroot, "work")
		if cleanWork != "" && strings.HasPrefix(filepath.Clean(workMount), cleanWork+string(os.PathSeparator)) {
			_ = chroot.UnmountPathWithSudo(workMount)
		}
		_ = chroot.UnmountWithSudo(c.imageChroot)
	}
	if c.bootDir != "" {
		if cleanWork != "" && strings.HasPrefix(filepath.Clean(c.bootDir), cleanWork+string(os.PathSeparator)) {
			_ = chroot.UnmountPathWithSudo(c.bootDir)
		} else {
			fmt.Fprintf(os.Stderr, "skipping unsafe boot unmount: %s\n", c.bootDir)
		}
	}
	if c.installDir != "" {
		if cleanWork != "" && strings.HasPrefix(filepath.Clean(c.installDir), cleanWork+string(os.PathSeparator)) {
			_ = chroot.UnmountPathWithSudo(c.installDir)
		} else {
			fmt.Fprintf(os.Stderr, "skipping unsafe install unmount: %s\n", c.installDir)
		}
	}
	if c.loopDev != "" {
		_ = image.UnmountLoop(c.loopDev)
	}
}
