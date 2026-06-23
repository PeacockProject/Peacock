package builder

import (
	"fmt"

	"peacock/internal/runner"
)

// unmountDeferred runs an unmount at defer time and LOGS a failure instead of
// discarding it — a bare `defer chroot.Unmount(p)` drops the returned error, so
// a stuck mount disappears silently and breaks the next build. Surface it to
// the build log (visible in the GUI) without failing the current teardown.
// Usage: defer unmountDeferred(path, chroot.UnmountWithSudo)
func unmountDeferred(path string, fn func(string) error) {
	if err := fn(path); err != nil {
		fmt.Fprintf(runner.LogWriter(),
			"warning: deferred unmount %s failed: %v (may leave a stale mount for the next run)\n",
			path, err)
	}
}
