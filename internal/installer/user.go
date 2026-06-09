package installer

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// CreateUser creates the human account inside targetRoot via chroot +
// useradd, then sets the password via chpasswd. Assumes the orchestrator
// already bind-mounted /proc /sys /dev into the target — userland
// configuration that doesn't write to /dev/random can sometimes succeed
// without binds, but chpasswd hashing wants /dev/urandom.
//
// Group set: wheel,audio,video,input. Distro-quirk: Debian doesn't ship
// `wheel` out of the box; we ignore failures on the group-add to keep
// things working there. PUNT: a more principled approach maps
// distro→groups; today wheel covers Arch + Fedora and `sudo` group is
// implicit via PAM on Debian.
func CreateUser(ctx context.Context, targetRoot string, spec UserSpec) error {
	if targetRoot == "" {
		return fmt.Errorf("installer: CreateUser: targetRoot required")
	}
	if !validUnixUsername(spec.Username) {
		return fmt.Errorf("installer: CreateUser: invalid username %q", spec.Username)
	}
	if spec.Password == "" {
		return fmt.Errorf("installer: CreateUser: password required")
	}

	// useradd -m -G wheel,audio,video,input -s /bin/bash -c "Full Name" <user>
	gecos := spec.Fullname
	if gecos == "" {
		gecos = spec.Username
	}
	useraddArgs := []string{
		"chroot", targetRoot,
		"useradd",
		"-m",
		"-G", "wheel,audio,video,input,sudo",
		"-s", "/bin/bash",
		"-c", gecos,
		spec.Username,
	}
	if err := runTagged(ctx, PhaseUserAndConfig, useraddArgs[0], useraddArgs[1:]...); err != nil {
		// Some distros lack `wheel` (Debian) or `sudo` (Arch). Retry with
		// only the groups likely to exist everywhere.
		logf(PhaseUserAndConfig, "useradd with full group set failed (%v); retrying with audio,video,input only", err)
		retry := []string{
			"chroot", targetRoot,
			"useradd",
			"-m",
			"-G", "audio,video,input",
			"-s", "/bin/bash",
			"-c", gecos,
			spec.Username,
		}
		if err2 := runTagged(ctx, PhaseUserAndConfig, retry[0], retry[1:]...); err2 != nil {
			return fmt.Errorf("useradd: %w", err2)
		}
	}

	// echo "user:pass" | chpasswd, inside the chroot.
	if err := chpasswdInChroot(ctx, targetRoot, spec.Username, spec.Password); err != nil {
		return fmt.Errorf("chpasswd: %w", err)
	}

	if spec.Autologin {
		if err := writeAutologinConfig(targetRoot, spec.Username); err != nil {
			// Non-fatal — login still works without autologin.
			logf(PhaseUserAndConfig, "autologin config failed: %v (continuing)", err)
		}
	}
	return nil
}

// chpasswdInChroot pipes "<user>:<pw>" into chroot <targetRoot> chpasswd.
// Done via exec directly (not runTagged) so we can set Stdin without
// leaking the password into the log writer.
func chpasswdInChroot(ctx context.Context, targetRoot, user, pw string) error {
	cmd := exec.CommandContext(ctx, "chroot", targetRoot, "chpasswd")
	cmd.Stdin = strings.NewReader(fmt.Sprintf("%s:%s\n", user, pw))
	// Log only that chpasswd ran — never the password.
	logf(PhaseUserAndConfig, "running chpasswd for %s (password redacted)", user)
	// We deliberately don't fan stdout/stderr through runTagged because
	// chpasswd is usually silent and we don't want to risk leaking the
	// password through a verbose error message.
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("chpasswd exit: %w (output: %s)", err, redactPassword(string(out), pw))
	}
	return nil
}

// redactPassword scrubs any literal occurrence of pw from s. Defensive —
// chpasswd shouldn't echo the password but if it ever did we don't want
// it in the error chain.
func redactPassword(s, pw string) string {
	if pw == "" {
		return s
	}
	return strings.ReplaceAll(s, pw, "***")
}

// writeAutologinConfig drops a small autologin snippet for whichever
// display manager the target ships. Detected by inspecting
// /etc/systemd/system/display-manager.service symlink target. Falls back
// to writing the sddm snippet (works whether or not sddm is installed,
// snippet is ignored if so).
func writeAutologinConfig(targetRoot, username string) error {
	dmLink := filepath.Join(targetRoot, "etc", "systemd", "system", "display-manager.service")
	dest, _ := os.Readlink(dmLink)
	dm := filepath.Base(dest)

	switch {
	case strings.Contains(dm, "sddm"):
		return writeSDDMAutologin(targetRoot, username)
	case strings.Contains(dm, "gdm"):
		return writeGDMAutologin(targetRoot, username)
	case strings.Contains(dm, "lightdm"):
		return writeLightDMAutologin(targetRoot, username)
	default:
		// Best effort: drop the sddm snippet anyway. If sddm isn't the
		// DM the file is harmless.
		return writeSDDMAutologin(targetRoot, username)
	}
}

func writeSDDMAutologin(targetRoot, username string) error {
	dir := filepath.Join(targetRoot, "etc", "sddm.conf.d")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	body := fmt.Sprintf("[Autologin]\nUser=%s\nSession=plasma\n", username)
	return os.WriteFile(filepath.Join(dir, "autologin.conf"), []byte(body), 0o644)
}

func writeGDMAutologin(targetRoot, username string) error {
	path := filepath.Join(targetRoot, "etc", "gdm3", "custom.conf")
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	body := fmt.Sprintf("[daemon]\nAutomaticLoginEnable=True\nAutomaticLogin=%s\n", username)
	return os.WriteFile(path, []byte(body), 0o644)
}

func writeLightDMAutologin(targetRoot, username string) error {
	dir := filepath.Join(targetRoot, "etc", "lightdm", "lightdm.conf.d")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	body := fmt.Sprintf("[Seat:*]\nautologin-user=%s\n", username)
	return os.WriteFile(filepath.Join(dir, "50-autologin.conf"), []byte(body), 0o644)
}
