package runner

import (
	"bytes"
	"context"
	"errors"
	"os/exec"
	"strings"
	"sync"
	"testing"
	"time"
)

// syncBuffer is a goroutine-safe write sink for the package log writer.
//
// A bare *bytes.Buffer is NOT safe here: exec.Cmd spawns one copy
// goroutine per non-*os.File stream, and runner wires the same log
// writer into stdout and stderr. With *bytes.Buffer the idle stream's
// io.Copy takes the buffer's ReaderFrom fast path and races the other
// stream's Write, observably losing data. Wrapping the buffer behind
// a mutex (and exposing only Write/String, so no promoted ReadFrom)
// is what any real shared-writer consumer of runner must do too.
type syncBuffer struct {
	mu  sync.Mutex
	buf bytes.Buffer
}

func (s *syncBuffer) Write(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.buf.Write(p)
}

func (s *syncBuffer) String() string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.buf.String()
}

// swapLogWriter installs a fresh syncBuffer as the package log writer
// and restores the previous writer when the test ends. The runner
// package keeps the writer in package-level state, so tests must never
// leave their buffer installed.
func swapLogWriter(t *testing.T) *syncBuffer {
	t.Helper()
	buf := &syncBuffer{}
	prev := LogWriter()
	SetLogWriter(buf)
	t.Cleanup(func() { SetLogWriter(prev) })
	return buf
}

func TestSetLogWriterRoundTrip(t *testing.T) {
	buf := swapLogWriter(t)
	if got := LogWriter(); got != buf {
		t.Fatalf("LogWriter() = %v, want the buffer just set", got)
	}
}

func TestSetLogWriterNilIsNoOp(t *testing.T) {
	buf := swapLogWriter(t)
	SetLogWriter(nil)
	if got := LogWriter(); got != buf {
		t.Fatalf("LogWriter() after SetLogWriter(nil) = %v, want previous writer kept", got)
	}
}

func TestLogf(t *testing.T) {
	buf := swapLogWriter(t)
	Logf("building %s (%d/%d)\n", "busybox", 1, 3)
	if got, want := buf.String(), "building busybox (1/3)\n"; got != want {
		t.Fatalf("Logf wrote %q, want %q", got, want)
	}
}

func TestLogln(t *testing.T) {
	buf := swapLogWriter(t)
	Logln("phase", 2, "done")
	if got, want := buf.String(), "phase 2 done\n"; got != want {
		t.Fatalf("Logln wrote %q, want %q", got, want)
	}
}

func TestRunRoutesStdoutToLogWriter(t *testing.T) {
	buf := swapLogWriter(t)
	if err := Run("sh", "-c", "echo out-from-runner"); err != nil {
		t.Fatalf("Run(echo) error = %v", err)
	}
	if !strings.Contains(buf.String(), "out-from-runner") {
		t.Fatalf("log writer got %q, want stdout routed into it", buf.String())
	}
}

func TestRunRoutesStderrToLogWriter(t *testing.T) {
	buf := swapLogWriter(t)
	if err := Run("sh", "-c", "echo err-from-runner >&2"); err != nil {
		t.Fatalf("Run error = %v", err)
	}
	if !strings.Contains(buf.String(), "err-from-runner") {
		t.Fatalf("log writer got %q, want stderr routed into it", buf.String())
	}
}

func TestRunFailureReturnsError(t *testing.T) {
	swapLogWriter(t)
	err := Run("sh", "-c", "exit 3")
	if err == nil {
		t.Fatal("Run(exit 3) = nil, want error")
	}
	var exitErr *exec.ExitError
	if !errors.As(err, &exitErr) {
		t.Fatalf("Run(exit 3) error = %T (%v), want *exec.ExitError", err, err)
	}
	if code := exitErr.ExitCode(); code != 3 {
		t.Fatalf("exit code = %d, want 3", code)
	}
}

func TestRunMissingBinaryReturnsError(t *testing.T) {
	swapLogWriter(t)
	if err := Run("/nonexistent/peacock-test-binary"); err == nil {
		t.Fatal("Run(missing binary) = nil, want error")
	}
}

func TestRunCmdDefaultsMissingWritersToLogWriter(t *testing.T) {
	buf := swapLogWriter(t)
	cmd := exec.Command("sh", "-c", "echo defaulted-stdout; echo defaulted-stderr >&2")
	if err := RunCmd(cmd); err != nil {
		t.Fatalf("RunCmd error = %v", err)
	}
	out := buf.String()
	if !strings.Contains(out, "defaulted-stdout") || !strings.Contains(out, "defaulted-stderr") {
		t.Fatalf("log writer got %q, want both streams defaulted into it", out)
	}
}

func TestRunCmdKeepsCallerWriters(t *testing.T) {
	logBuf := swapLogWriter(t)
	var cmdBuf bytes.Buffer
	cmd := exec.Command("sh", "-c", "echo caller-owned")
	cmd.Stdout = &cmdBuf
	if err := RunCmd(cmd); err != nil {
		t.Fatalf("RunCmd error = %v", err)
	}
	if !strings.Contains(cmdBuf.String(), "caller-owned") {
		t.Fatalf("caller stdout got %q, want command output", cmdBuf.String())
	}
	if strings.Contains(logBuf.String(), "caller-owned") {
		t.Fatalf("log writer got %q, want caller-set stdout respected", logBuf.String())
	}
}

func TestRunOutputReturnsAndLogsStdout(t *testing.T) {
	buf := swapLogWriter(t)
	out, err := RunOutput(exec.Command("sh", "-c", "echo captured-output"))
	if err != nil {
		t.Fatalf("RunOutput error = %v", err)
	}
	if !strings.Contains(out, "captured-output") {
		t.Fatalf("RunOutput returned %q, want stdout", out)
	}
	if !strings.Contains(buf.String(), "captured-output") {
		t.Fatalf("log writer got %q, want stdout mirrored into it", buf.String())
	}
}

// clearExecPrefix restores direct exec at the end of a test so a stray
// prefix can't leak into other tests (runner keeps it in package state).
func clearExecPrefixOnCleanup(t *testing.T) {
	t.Helper()
	t.Cleanup(ClearExecPrefix)
}

func TestSetExecPrefixRoundTrip(t *testing.T) {
	clearExecPrefixOnCleanup(t)
	if got := ExecPrefix(); got != nil {
		t.Fatalf("ExecPrefix() default = %v, want nil", got)
	}
	SetExecPrefix([]string{"sudo", "chroot", "/host"})
	got := ExecPrefix()
	want := []string{"sudo", "chroot", "/host"}
	if len(got) != len(want) {
		t.Fatalf("ExecPrefix() = %v, want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("ExecPrefix()[%d] = %q, want %q", i, got[i], want[i])
		}
	}
}

func TestSetExecPrefixDefensiveCopy(t *testing.T) {
	clearExecPrefixOnCleanup(t)
	src := []string{"sudo", "chroot", "/host"}
	SetExecPrefix(src)
	src[2] = "/tampered" // mutate caller's slice after the set
	if got := ExecPrefix(); got[2] != "/host" {
		t.Fatalf("ExecPrefix()[2] = %q, want %q (set must copy defensively)", got[2], "/host")
	}
	// ExecPrefix's return must also be a copy.
	ret := ExecPrefix()
	ret[0] = "tampered"
	if got := ExecPrefix(); got[0] != "sudo" {
		t.Fatalf("ExecPrefix()[0] = %q, want %q (getter must return a copy)", got[0], "sudo")
	}
}

// TestRunWithExecPrefixBuildsRightArgv uses an `echo` prefix so the
// wrapped command actually executes: Run("hello", "world") becomes
// `echo hello world`, and we assert the captured output.
func TestRunWithExecPrefixBuildsRightArgv(t *testing.T) {
	buf := swapLogWriter(t)
	clearExecPrefixOnCleanup(t)
	SetExecPrefix([]string{"echo"})
	if err := Run("hello", "world"); err != nil {
		t.Fatalf("Run with echo prefix error = %v", err)
	}
	if got := strings.TrimSpace(buf.String()); got != "hello world" {
		t.Fatalf("echo-prefixed Run wrote %q, want %q", got, "hello world")
	}
}

func TestRunCmdWithExecPrefixBuildsRightArgv(t *testing.T) {
	buf := swapLogWriter(t)
	clearExecPrefixOnCleanup(t)
	SetExecPrefix([]string{"echo"})
	if err := RunCmd(exec.Command("hi", "there")); err != nil {
		t.Fatalf("RunCmd with echo prefix error = %v", err)
	}
	if got := strings.TrimSpace(buf.String()); got != "hi there" {
		t.Fatalf("echo-prefixed RunCmd wrote %q, want %q", got, "hi there")
	}
}

func TestRunOutputWithExecPrefixBuildsRightArgv(t *testing.T) {
	swapLogWriter(t)
	clearExecPrefixOnCleanup(t)
	SetExecPrefix([]string{"echo"})
	out, err := RunOutput(exec.Command("captured", "value"))
	if err != nil {
		t.Fatalf("RunOutput with echo prefix error = %v", err)
	}
	if got := strings.TrimSpace(out); got != "captured value" {
		t.Fatalf("echo-prefixed RunOutput returned %q, want %q", got, "captured value")
	}
}

func TestClearExecPrefixRestoresDirectExec(t *testing.T) {
	buf := swapLogWriter(t)
	clearExecPrefixOnCleanup(t)
	SetExecPrefix([]string{"echo"})
	ClearExecPrefix()
	if got := ExecPrefix(); got != nil {
		t.Fatalf("ExecPrefix() after clear = %v, want nil", got)
	}
	// With the prefix cleared, sh runs directly (not echoed).
	if err := Run("sh", "-c", "echo direct-exec"); err != nil {
		t.Fatalf("Run after ClearExecPrefix error = %v", err)
	}
	if got := strings.TrimSpace(buf.String()); got != "direct-exec" {
		t.Fatalf("after ClearExecPrefix got %q, want direct exec output %q", got, "direct-exec")
	}
}

func TestSetExecPrefixEmptyClears(t *testing.T) {
	clearExecPrefixOnCleanup(t)
	SetExecPrefix([]string{"echo"})
	SetExecPrefix(nil)
	if got := ExecPrefix(); got != nil {
		t.Fatalf("ExecPrefix() after SetExecPrefix(nil) = %v, want nil", got)
	}
	SetExecPrefix([]string{"echo"})
	SetExecPrefix([]string{})
	if got := ExecPrefix(); got != nil {
		t.Fatalf("ExecPrefix() after SetExecPrefix([]) = %v, want nil", got)
	}
}

// TestSetExecPrefixRaceFree hammers SetExecPrefix/ExecPrefix/Clear from
// multiple goroutines so `go test -race` proves the mutex coverage.
func TestSetExecPrefixRaceFree(t *testing.T) {
	clearExecPrefixOnCleanup(t)
	var wg sync.WaitGroup
	for i := 0; i < 16; i++ {
		wg.Add(1)
		go func(n int) {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				SetExecPrefix([]string{"sudo", "chroot", "/host"})
				_ = ExecPrefix()
				ClearExecPrefix()
			}
		}(i)
	}
	wg.Wait()
}

func TestSetContextNilIsNoOp(t *testing.T) {
	SetContext(nil)
	swapLogWriter(t)
	if err := Run("sh", "-c", "exit 0"); err != nil {
		t.Fatalf("Run after SetContext(nil) error = %v, want commands still runnable", err)
	}
}

// TestContextCancellationKillsCommand pins runCmd's teardown: when the
// package context is cancelled mid-command, the process group gets
// SIGTERMed and Run returns promptly instead of waiting out the child.
func TestContextCancellationKillsCommand(t *testing.T) {
	swapLogWriter(t)

	ctx, cancel := context.WithCancel(context.Background())
	SetContext(ctx)
	t.Cleanup(func() {
		cancel()
		SetContext(context.Background())
	})

	timer := time.AfterFunc(100*time.Millisecond, cancel)
	defer timer.Stop()

	start := time.Now()
	err := Run("sh", "-c", "sleep 30")
	elapsed := time.Since(start)

	if err == nil {
		t.Fatal("Run(sleep 30) with cancelled context = nil, want error")
	}
	if elapsed > 10*time.Second {
		t.Fatalf("Run took %v after cancellation, want prompt return", elapsed)
	}
}
