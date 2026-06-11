package pipeline

// flash.go — chroot-based device flashing so the host needs no fastboot /
// heimdall installed. The flash tools live in a persistent host-arch chroot
// (sibling of the build chroots); USB reaches them via a /dev/bus/usb
// bind-mount, since a chroot shares the host kernel and devtmpfs.
//
// This file holds the foundation: ensuring the chroot + tools, USB
// passthrough, and device detection. The two-phase flash pipeline (flash
// bootloader via flash_method → reboot/poll → fastboot the rest) builds on
// these primitives.

import (
	"bytes"
	"fmt"
	"os/exec"
	"path/filepath"
	"strings"

	"peacock/internal/builder"
	"peacock/internal/runner"
)

// FlashTool maps a device.toml flash_method to the stage-1 bootstrap tool
// used to get our bootloader onto the device. Everything after the bootloader
// is flashed with fastboot (lk2nd/minkernel provide their own fastboot once no
// bootable system is present), so heimdall is only ever this first step.
//
//	"heimdall-*" -> "heimdall"   (Samsung Download Mode)
//	anything else -> "fastboot"  (the default)
func FlashTool(flashMethod string) string {
	if strings.HasPrefix(strings.ToLower(flashMethod), "heimdall") {
		return "heimdall"
	}
	return "fastboot"
}

// flashChrootDir is the persistent flash chroot path: a host-arch sibling of
// the package store, e.g. <var>/flash-chroot/x86_64.
func flashChrootDir(b *builder.Builder) string {
	return filepath.Join(filepath.Dir(b.CacheDir), "flash-chroot", builder.HostArchString())
}

// EnsureFlashChroot ensures a persistent host-arch chroot with android-tools
// (fastboot) + heimdall installed, and returns its root. Idempotent: the tool
// install is skipped once both binaries are present.
func EnsureFlashChroot(b *builder.Builder) (string, error) {
	host := builder.HostArchString()
	root := flashChrootDir(b)
	if err := b.EnsureBuildChroot(root, host, false); err != nil {
		return "", fmt.Errorf("ensuring flash chroot: %w", err)
	}
	haveFastboot := fileExists(filepath.Join(root, "usr", "bin", "fastboot"))
	haveHeimdall := fileExists(filepath.Join(root, "usr", "bin", "heimdall"))
	if !haveFastboot || !haveHeimdall {
		runner.Logln("Installing flash tools (android-tools, heimdall) into flash chroot...")
		// bootstrapPacmanPackages does keyring init + a host-side `pacman -r`
		// install into the chroot, downloading through the persistent per-arch
		// distro cache so this is a one-time cost.
		if err := bootstrapPacmanPackages(b, root, []string{"android-tools", "heimdall"}); err != nil {
			return "", fmt.Errorf("installing flash tools: %w", err)
		}
	}
	return root, nil
}

// mountUSB bind-mounts the host USB bus into the chroot so fastboot/heimdall
// (run as root inside) can see plugged-in devices. Hotplugged nodes appear
// automatically — it's the same host devtmpfs. Returns an unmount cleanup.
func mountUSB(root string) (func(), error) {
	target := filepath.Join(root, "dev", "bus", "usb")
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", target)); err != nil {
		return nil, fmt.Errorf("creating usb mount point: %w", err)
	}
	if !isMounted(target) {
		if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", "/dev/bus/usb", target)); err != nil {
			return nil, fmt.Errorf("bind-mounting /dev/bus/usb: %w", err)
		}
	}
	return func() { _ = runner.RunCmd(exec.Command("sudo", "umount", target)) }, nil
}

// FlashDetect lists devices visible to the given tool inside the flash chroot.
// tool is "fastboot" or "heimdall". For fastboot it returns the device
// serials; for heimdall (which has no serial concept in detect) it returns a
// single "download-mode" sentinel when a device is present. An empty slice
// means nothing is connected (not an error).
func FlashDetect(root, tool string) ([]string, error) {
	cleanup, err := mountUSB(root)
	if err != nil {
		return nil, err
	}
	defer cleanup()

	var out bytes.Buffer
	if tool == "heimdall" {
		// `heimdall detect` exits 0 and prints "Device detected" when a phone
		// is in Download Mode, non-zero otherwise.
		cmd := exec.Command("sudo", "chroot", root, "heimdall", "detect")
		cmd.Stdout = &out
		cmd.Stderr = &out
		_ = cmd.Run()
		if strings.Contains(out.String(), "Device detected") {
			return []string{"download-mode"}, nil
		}
		return nil, nil
	}

	cmd := exec.Command("sudo", "chroot", root, "fastboot", "devices")
	cmd.Stdout = &out
	cmd.Stderr = &out
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("fastboot devices: %w (%s)", err, strings.TrimSpace(out.String()))
	}
	var serials []string
	for _, line := range strings.Split(out.String(), "\n") {
		fields := strings.Fields(line)
		// Lines look like: "SERIAL\tfastboot"
		if len(fields) >= 2 && fields[1] == "fastboot" && fields[0] != "" {
			serials = append(serials, fields[0])
		}
	}
	return serials, nil
}
