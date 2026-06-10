package runner

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"sync"
	"syscall"
	"time"
)

var (
	logWriter io.Writer       = os.Stdout
	ctx       context.Context = context.Background()
	mu        sync.Mutex

	// execPrefix, when non-empty, wraps EVERY command run through this
	// package. Guarded by mu (same as logWriter). See SetExecPrefix.
	execPrefix []string
)

// SetExecPrefix sets a command prefix that wraps every command run
// through this package. When set to e.g.
// []string{"sudo", "chroot", "/path/to/host-chroot"}, a call to
// Run("make", "-j4") executes `sudo chroot /path/to/host-chroot make -j4`,
// and any *exec.Cmd handed to RunCmd / RunOutput is rewritten in place
// so its argv is prefixed too.
//
// Passing nil or an empty slice clears the prefix, restoring direct
// exec (the default, byte-identical to no prefix at all).
//
// The slice is copied defensively; later mutation of the caller's slice
// has no effect.
//
// Gotchas when the prefix routes commands into a chroot (e.g.
// `sudo chroot <root>`):
//
//   - cmd.Dir: a working directory is set on the HOST side, but the
//     wrapped command runs inside the chroot's mount namespace, so the
//     host path won't resolve unless it is bind-mounted into the chroot
//     at the SAME path. Callers that set cmd.Dir to a workdir/port path
//     that the host-chroot bootstrap bind-mounts at the identical path
//     are fine; others will break. This package does not rewrite cmd.Dir.
//
//   - cmd.Env and absolute paths in args: passed through to the chroot
//     verbatim. The bind-mount strategy (owned by internal/host) ensures
//     the workdir and ports exist at the same absolute path inside the
//     chroot, so absolute paths line up. This is an assumption, not an
//     enforced invariant.
func SetExecPrefix(prefix []string) {
	mu.Lock()
	defer mu.Unlock()
	if len(prefix) == 0 {
		execPrefix = nil
		return
	}
	cp := make([]string, len(prefix))
	copy(cp, prefix)
	execPrefix = cp
}

// ExecPrefix returns a copy of the current exec prefix (nil when none
// is set). Mutating the returned slice has no effect on the package
// state.
func ExecPrefix() []string {
	mu.Lock()
	defer mu.Unlock()
	if len(execPrefix) == 0 {
		return nil
	}
	cp := make([]string, len(execPrefix))
	copy(cp, execPrefix)
	return cp
}

// ClearExecPrefix removes any exec prefix, restoring direct exec.
func ClearExecPrefix() {
	mu.Lock()
	defer mu.Unlock()
	execPrefix = nil
}

// applyExecPrefix rewrites cmd in place so its argv is wrapped by the
// current exec prefix. A no-op when no prefix is set, leaving cmd
// byte-identical to direct exec. cmd.Path is re-resolved to the prefix's
// first element via exec.LookPath; if that lookup fails the path is set
// to the bare prefix element so cmd.Start surfaces a clear error rather
// than silently running the wrong binary.
func applyExecPrefix(cmd *exec.Cmd) {
	prefix := ExecPrefix()
	if len(prefix) == 0 {
		return
	}
	newArgs := make([]string, 0, len(prefix)+len(cmd.Args))
	newArgs = append(newArgs, prefix...)
	newArgs = append(newArgs, cmd.Args...)
	cmd.Args = newArgs
	if p, err := exec.LookPath(prefix[0]); err == nil {
		cmd.Path = p
		// exec.Command stashes a lookPathErr when the original name
		// wasn't found on PATH (e.g. a chroot-internal binary that only
		// exists inside the chroot). Since we've re-resolved Path to the
		// prefix's first element, that stale error must be cleared or
		// cmd.Start() would surface it instead of running.
		cmd.Err = nil
	} else {
		cmd.Path = prefix[0]
		cmd.Err = err
	}
}

// SetLogWriter sets the writer for command stdout/stderr.
func SetLogWriter(w io.Writer) {
	if w == nil {
		return
	}
	mu.Lock()
	defer mu.Unlock()
	logWriter = w
}

// LogWriter returns the current log writer.
func LogWriter() io.Writer {
	mu.Lock()
	defer mu.Unlock()
	return logWriter
}

// Logf writes formatted user-facing progress output to the current
// log writer. Behaves like fmt.Printf when no writer override is set
// (the default writer is os.Stdout).
func Logf(format string, args ...interface{}) {
	fmt.Fprintf(LogWriter(), format, args...)
}

// Logln writes user-facing progress output to the current log writer.
// Behaves like fmt.Println when no writer override is set.
func Logln(args ...interface{}) {
	fmt.Fprintln(LogWriter(), args...)
}

// SetContext sets the context used for command execution.
func SetContext(c context.Context) {
	if c == nil {
		return
	}
	ctx = c
}

// wmu serializes writes to the configured log writer. os/exec spawns
// one copy goroutine per non-*os.File output stream, so a command
// whose stdout AND stderr target the same io.Writer races unless every
// write takes this lock. The GUI's MultiWriter(logFile, eventEmitter)
// is exactly that case.
var wmu sync.Mutex

type syncWriter struct{ w io.Writer }

func (s syncWriter) Write(p []byte) (int, error) {
	wmu.Lock()
	defer wmu.Unlock()
	return s.w.Write(p)
}

// lockedWriter returns the current log writer wrapped for concurrent
// use. *os.File values (the default os.Stdout) are returned bare so
// os/exec can hand the fd straight to the child process — no copy
// goroutine, no race, no behavior change for the CLI.
func lockedWriter() io.Writer {
	w := LogWriter()
	if f, ok := w.(*os.File); ok {
		return f
	}
	return syncWriter{w: w}
}

// Run runs the command and streams stdout/stderr to the log writer.
func Run(name string, args ...string) error {
	cmd := exec.Command(name, args...)
	w := lockedWriter()
	cmd.Stdout = w
	cmd.Stderr = w
	return runCmd(cmd)
}

// RunCmd runs the provided command, wiring stdout/stderr to the log writer.
func RunCmd(cmd *exec.Cmd) error {
	if cmd.Stdout == nil {
		cmd.Stdout = lockedWriter()
	}
	if cmd.Stderr == nil {
		cmd.Stderr = lockedWriter()
	}
	return runCmd(cmd)
}

// RunOutput runs a command and returns its stdout while logging it too.
func RunOutput(cmd *exec.Cmd) (string, error) {
	var buf bytes.Buffer
	cmd.Stdout = io.MultiWriter(&buf, lockedWriter())
	if cmd.Stderr == nil {
		cmd.Stderr = lockedWriter()
	}
	err := runCmd(cmd)
	return buf.String(), err
}

func runCmd(cmd *exec.Cmd) error {
	// Wrap the command in the configured exec prefix (e.g.
	// `sudo chroot <root>`). No-op when no prefix is set. This is the
	// single funnel all three entry points (Run/RunCmd/RunOutput) reach,
	// so every command is wrapped here exactly once.
	applyExecPrefix(cmd)

	// Ensure separate process group for signal cleanup.
	if cmd.SysProcAttr == nil {
		cmd.SysProcAttr = &syscall.SysProcAttr{}
	}
	cmd.SysProcAttr.Setpgid = true

	if err := cmd.Start(); err != nil {
		return err
	}

	done := make(chan error, 1)
	go func() {
		done <- cmd.Wait()
	}()

	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM)
		select {
		case err := <-done:
			return err
		case <-time.After(2 * time.Second):
			_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
			return <-done
		}
	}
}
