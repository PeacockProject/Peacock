package pipeline

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"peacock/internal/chroot"
	"peacock/internal/image"
	"peacock/internal/runner"
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
	// Best-effort but NOT silent: a swallowed unmount/detach failure leaves a
	// stale mount or loop device that breaks the NEXT build mysteriously. Log
	// each failure (to the build log, so it's visible in the GUI too) and keep
	// going so one stuck step can't trap the rest of the teardown.
	warn := func(what string, err error) {
		if err != nil {
			fmt.Fprintf(runner.LogWriter(),
				"cleanup: %s failed: %v (continuing; this may leave a stale mount/loop for the next run)\n",
				what, err)
		}
	}
	if c.imageChroot != "" {
		workMount := filepath.Join(c.imageChroot, "work")
		if underWorkDir(c.workDir, workMount) {
			warn("unmount "+workMount, chroot.UnmountPathWithSudo(workMount))
		}
		warn("unmount "+c.imageChroot, chroot.UnmountWithSudo(c.imageChroot))
	}
	if c.bootDir != "" {
		if underWorkDir(c.workDir, c.bootDir) {
			warn("unmount "+c.bootDir, chroot.UnmountPathWithSudo(c.bootDir))
		} else {
			fmt.Fprintf(os.Stderr, "skipping unsafe boot unmount: %s\n", c.bootDir)
		}
	}
	if c.installDir != "" {
		if underWorkDir(c.workDir, c.installDir) {
			warn("unmount "+c.installDir, chroot.UnmountPathWithSudo(c.installDir))
		} else {
			fmt.Fprintf(os.Stderr, "skipping unsafe install unmount: %s\n", c.installDir)
		}
	}
	if c.loopDev != "" {
		warn("detach loop "+c.loopDev, image.UnmountLoop(c.loopDev))
	}
}

// underWorkDir reports whether path is strictly nested inside workDir
// after lexical cleaning. It is the guard that keeps a corrupted or
// adversarial mountpoint string from making Cleanup unmount host paths
// outside the Peacock work directory. The workDir itself does not
// count as "under" it, and an empty workDir (cleaned to ".") never
// matches an absolute path.
func underWorkDir(workDir, path string) bool {
	cleanWork := filepath.Clean(workDir)
	return cleanWork != "" && strings.HasPrefix(filepath.Clean(path), cleanWork+string(os.PathSeparator))
}
