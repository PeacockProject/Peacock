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
)

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
