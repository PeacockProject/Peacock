package installer

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os/exec"
	"strings"
	"sync"

	"peacock/internal/runner"
)

// runTagged runs cmd with stdout/stderr fanned out to (a) the package-level
// runner.LogWriter so the GUI's terminal view picks it up, and (b) optionally
// to lineSink for parsing (rsync progress, etc.). Each line is prefixed with
// `[installer/<phase>] ` so the GUI's log view stays grep-able.
//
// Cancellation: cmd is built with exec.CommandContext under the caller's ctx,
// so ctx.Done() kills the subprocess.
func runTagged(ctx context.Context, phase Phase, name string, args ...string) error {
	cmd := exec.CommandContext(ctx, name, args...)
	return runTaggedCmd(ctx, phase, cmd, nil)
}

// runTaggedCmd is the lower-level variant. lineSink, if non-nil, receives
// each output line *unprefixed* so parsers don't have to strip the tag.
func runTaggedCmd(ctx context.Context, phase Phase, cmd *exec.Cmd, lineSink func(string)) error {
	if cmd.Stdout != nil || cmd.Stderr != nil {
		return fmt.Errorf("installer: runTaggedCmd refuses to overwrite preset stdout/stderr on %s", cmd.Path)
	}

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return fmt.Errorf("installer: stdout pipe: %w", err)
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return fmt.Errorf("installer: stderr pipe: %w", err)
	}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("installer: start %s: %w", cmd.Path, err)
	}

	var wg sync.WaitGroup
	wg.Add(2)
	go pumpLines(&wg, phase, stdout, lineSink)
	go pumpLines(&wg, phase, stderr, lineSink)

	waitErr := cmd.Wait()
	wg.Wait()
	if ctx.Err() != nil {
		return ctx.Err()
	}
	return waitErr
}

// pumpLines reads r line-by-line, tags each line, writes it to the runner
// log writer, and forwards the untagged line to sink when present.
//
// We use a custom reader (not bufio.Scanner) so we can handle rsync's
// CR-overwrites in --info=progress2 output. Scanner only splits on LF;
// progress2 emits "\r" between updates, which would otherwise pile up
// into one giant line.
func pumpLines(wg *sync.WaitGroup, phase Phase, r io.Reader, sink func(string)) {
	defer wg.Done()
	w := runner.LogWriter()
	tag := fmt.Sprintf("[installer/%s] ", phase)

	br := bufio.NewReader(r)
	var buf strings.Builder
	flush := func() {
		if buf.Len() == 0 {
			return
		}
		line := buf.String()
		buf.Reset()
		if w != nil {
			fmt.Fprintln(w, tag+line)
		}
		if sink != nil {
			sink(line)
		}
	}
	for {
		b, err := br.ReadByte()
		if err != nil {
			flush()
			return
		}
		switch b {
		case '\n', '\r':
			flush()
		default:
			buf.WriteByte(b)
		}
	}
}

// logf writes a synthetic line to the runner log writer with the installer
// phase tag — used to mark phase boundaries in the log even when no
// subprocess is running.
func logf(phase Phase, format string, args ...interface{}) {
	w := runner.LogWriter()
	if w == nil {
		return
	}
	fmt.Fprintf(w, "[installer/%s] %s\n", phase, fmt.Sprintf(format, args...))
}
