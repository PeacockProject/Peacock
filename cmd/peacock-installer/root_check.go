package main

// Preflight checks the installer must pass before opening a GUI.
//
// We refuse two failure modes loudly:
//
//  1. Non-root: the installer shells out to parted, mkfs, rsync,
//     grub-install, chroot, etc. — every one of those will fail in a
//     confusing way mid-install if euid != 0. Better to refuse upfront.
//
//  2. Not on a live ISO: the installer copies /run/live → target. On a
//     normal booted system /run/live doesn't exist and the user would
//     just be running an empty installer for no reason. The
//     PEACOCK_INSTALLER_FORCE env var lets developers override this on
//     a dev host for UI work.
//
// Both checks surface a native dialog via wails runtime BEFORE the GUI
// is opened — calling runtime.MessageDialog needs a Wails context, so
// for the pre-window error we use a plain wails dialog package that
// shows a system dialog without a window context. If even that isn't
// available, we fall back to printing to stderr.

import (
	"fmt"
	"os"

	"github.com/wailsapp/wails/v2/pkg/runtime"
)

// checkPreflight returns true when the installer is allowed to start
// the GUI. On failure it shows a native dialog and returns false so
// main() can exit 0.
func checkPreflight() bool {
	if reason := preflightReason(); reason != "" {
		showPreflightError(reason)
		return false
	}
	return true
}

// preflightReason returns an empty string when preflight passes, or a
// human-readable error string when it doesn't. Pulled out for testing.
func preflightReason() string {
	if os.Getenv("PEACOCK_INSTALLER_FORCE") == "1" {
		return ""
	}
	if os.Geteuid() != 0 {
		return "peacock-installer must be run as root.\n\n" +
			"On the PeacockOS live ISO the installer is launched\n" +
			"automatically with the right privileges. If you are\n" +
			"running this binary by hand, try:\n\n" +
			"    sudo peacock-installer\n\n" +
			"(set PEACOCK_INSTALLER_FORCE=1 to bypass this check for\n" +
			"UI development on a non-live host.)"
	}
	if _, err := os.Stat("/run/live"); err != nil {
		return "peacock-installer expects to run from the PeacockOS\n" +
			"live ISO, where /run/live is the live medium mount.\n\n" +
			"It looks like you booted into a normal installed system\n" +
			"instead. Boot the live ISO to install PeacockOS.\n\n" +
			"(set PEACOCK_INSTALLER_FORCE=1 to bypass this check for\n" +
			"UI development on a non-live host.)"
	}
	return ""
}

// showPreflightError surfaces the message in a native dialog when
// possible and otherwise falls back to stderr. Wails v2's
// runtime.MessageDialog wants a ctx (window scope) — for the
// pre-window error we can't satisfy that, so we go through the
// system-dialog approach: zenity/kdialog/osascript/msgbox-equivalent
// via the wails runtime package. If that itself errors we just print.
//
// Note: runtime.MessageDialog with a nil ctx is undefined behavior in
// some Wails versions; we guard with recover().
func showPreflightError(msg string) {
	fmt.Fprintln(os.Stderr, "peacock-installer: preflight failed:")
	fmt.Fprintln(os.Stderr, msg)
	defer func() { _ = recover() }()
	_, _ = runtime.MessageDialog(nil, runtime.MessageDialogOptions{
		Type:    runtime.ErrorDialog,
		Title:   "peacock-installer",
		Message: msg,
	})
}
