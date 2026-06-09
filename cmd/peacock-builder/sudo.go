package main

// Sudo handling for the GUI build path.
//
// The CLI `peacock build` shells out to a mix of tools that need root —
// loop-mount, chroot, mkfs, partprobe, etc. When run from a terminal, sudo
// prompts on the TTY and the user types a password. When run from a Wails
// GUI launched out of a desktop launcher there is no TTY, so any sudo
// invocation blocks forever waiting on stdin.
//
// This file centralises the workaround. EnsureBuildPrivileges is called
// from app.startup and arranges for subsequent `sudo` calls to succeed
// non-interactively for the lifetime of the GUI. The mechanics differ by
// platform:
//
//   * Linux: rely on the sudo credential cache (`sudo -v`). If we already
//     have one (`sudo -n true` succeeds), we just refresh it periodically.
//     Otherwise we shell out to pkexec which pops a polkit auth dialog;
//     once authenticated we run `sudo -v` to seed the cache, then
//     refresh.
//   * macOS: same credential-cache idea but the auth dialog is driven by
//     `osascript` invoking `do shell script ... with administrator
//     privileges` — that's the standard Cocoa path for one-shot escalation
//     from a GUI app. A future enhancement is the Security.framework
//     AuthorizationExecuteWithPrivileges call; deprecated but still
//     functional and avoids the AppleScript shell.
//   * Windows: privilege elevation is done at process launch time via the
//     UAC manifest in the .exe — see wails.json for the asInvoker setting.
//     Nothing to do at runtime; we just record the mode for the GUI to
//     display.
//
// On failure EnsureBuildPrivileges returns a non-nil error with an
// actionable message; the App struct stashes it and the React side reads
// it via App.PrivilegeError() to render a friendly panel.

import (
	"context"
	"errors"
	"fmt"
	"os/exec"
	"runtime"
	"time"
)

// sudoRefreshInterval is how often we run `sudo -v` in the background to
// keep the credential cache warm. Default sudo timestamp_timeout is 5
// minutes, so refreshing every 4 leaves headroom for clock skew / slow
// builds between refreshes.
const sudoRefreshInterval = 4 * time.Minute

// SudoPrivilegeMode returns a short human string describing how
// privileges are being managed on the current platform. The GUI displays
// this in the "Host check" tile so the user knows whether they should
// expect a polkit popup, an osascript dialog, or a UAC prompt later.
func SudoPrivilegeMode() string {
	switch runtime.GOOS {
	case "linux":
		return "linux: sudo credential cache + pkexec fallback"
	case "darwin":
		return "macOS: sudo credential cache + osascript admin prompt"
	case "windows":
		return "windows: UAC manifest (asInvoker)"
	default:
		return runtime.GOOS + ": unsupported; sudo calls will likely block"
	}
}

// EnsureBuildPrivileges arranges for the build's sudo invocations to
// succeed without re-prompting per-call. Returns nil on success, an error
// with an actionable message on failure.
//
// The ctx governs the background refresh goroutine: cancelling it stops
// the periodic `sudo -v`. Wails passes a window-lifetime context to
// app.startup which is appropriate here.
func EnsureBuildPrivileges(ctx context.Context) error {
	switch runtime.GOOS {
	case "linux":
		return ensureBuildPrivilegesLinux(ctx)
	case "darwin":
		return ensureBuildPrivilegesDarwin(ctx)
	case "windows":
		// UAC handles elevation at process launch via the manifest;
		// nothing to do at runtime. See wails.json for the configured
		// requestedExecutionLevel.
		//
		// TODO: Windows builds aren't really exercised yet — when we
		// stand up a Windows CI runner we need to actually test the
		// manifest, verify that asInvoker is the right level (vs.
		// requireAdministrator), and confirm sudo equivalents (e.g.
		// runas / Start-Process -Verb RunAs) work from the GUI.
		return nil
	default:
		return fmt.Errorf("unsupported platform %q: don't know how to acquire build privileges", runtime.GOOS)
	}
}

// ensureBuildPrivilegesLinux: first try the existing sudo credential
// cache; if that misses, fall back to pkexec to drive a polkit dialog.
// Once we have a valid cache, spawn a goroutine to refresh it every
// sudoRefreshInterval so long-running builds don't hit the 5-minute sudo
// timeout mid-pipeline.
func ensureBuildPrivilegesLinux(ctx context.Context) error {
	if sudoCacheValid() {
		go refreshSudoLoop(ctx)
		return nil
	}

	// No cached credential — try pkexec. We invoke `pkexec sudo -v`
	// rather than `pkexec true` because we want the *sudo* timestamp
	// populated (so subsequent `sudo` calls from the build subprocess
	// see a valid cache); pkexec alone only grants the pkexec
	// transaction.
	pkexec, err := exec.LookPath("pkexec")
	if err != nil {
		return errors.New("could not acquire build privileges: no cached sudo credentials and pkexec is not installed. Run the GUI from a terminal where sudo was authenticated, or install polkit")
	}

	cmd := exec.CommandContext(ctx, pkexec, "sudo", "-v")
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("pkexec failed to acquire build privileges: %w (output: %s). Run the GUI from a terminal where sudo was authenticated, or install polkit", err, string(out))
	}

	if !sudoCacheValid() {
		return errors.New("pkexec returned success but sudo credentials are still missing; refusing to proceed")
	}

	go refreshSudoLoop(ctx)
	return nil
}

// ensureBuildPrivilegesDarwin: same shape as Linux but the auth dialog is
// driven by osascript instead of pkexec. The AppleScript snippet asks
// macOS to run `sudo -v` with administrator privileges, which opens the
// standard Cocoa credential dialog.
func ensureBuildPrivilegesDarwin(ctx context.Context) error {
	if sudoCacheValid() {
		go refreshSudoLoop(ctx)
		return nil
	}

	// The osascript invocation: see the verbatim string in
	// macOSSudoAppleScript below for the exact argv.
	cmd := exec.CommandContext(ctx, "osascript", "-e", macOSSudoAppleScript)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("osascript failed to acquire build privileges: %w (output: %s). Cancel the prompt or run the GUI from a terminal where sudo was authenticated", err, string(out))
	}

	if !sudoCacheValid() {
		return errors.New("osascript returned success but sudo credentials are still missing; refusing to proceed")
	}

	go refreshSudoLoop(ctx)
	return nil
}

// macOSSudoAppleScript is the verbatim AppleScript passed to `osascript
// -e`. Kept as a constant so it's easy to audit (the user-visible
// password dialog text is controlled by macOS, not us).
const macOSSudoAppleScript = `do shell script "sudo -v" with administrator privileges`

// sudoCacheValid reports whether `sudo -n true` succeeds — i.e. sudo can
// run without prompting because the credential cache is populated.
func sudoCacheValid() bool {
	cmd := exec.Command("sudo", "-n", "true")
	return cmd.Run() == nil
}

// refreshSudoLoop runs `sudo -v` every sudoRefreshInterval to keep the
// credential cache warm for the lifetime of ctx. Errors are intentionally
// swallowed: if the cache lapses mid-build the user will see a sudo
// prompt failure from the build subprocess, which is a better signal
// than a silent goroutine log.
func refreshSudoLoop(ctx context.Context) {
	ticker := time.NewTicker(sudoRefreshInterval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			// Best-effort. Ignore errors — see comment above.
			_ = exec.CommandContext(ctx, "sudo", "-v").Run()
		}
	}
}
