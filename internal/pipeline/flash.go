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
	"io"
	"os/exec"
	"path/filepath"
	"strings"

	"peacock/internal/builder"
	"peacock/internal/runner"
)

// The flash chroot is a minimal Alpine root: tiny (tens of MB vs the Arch
// build chroot's GBs) and self-contained — the minirootfs ships apk, so no
// host apk is needed (apk add runs inside the chroot, which is host-arch).
const (
	alpineBranch  = "v3.21"
	alpineRelease = "3.21.7"
	alpineMirror  = "https://dl-cdn.alpinelinux.org/alpine"
)

// alpineMinirootfsURL is the CDN URL for the host-arch minirootfs tarball.
func alpineMinirootfsURL(arch string) string {
	return fmt.Sprintf("%s/%s/releases/%s/alpine-minirootfs-%s-%s.tar.gz",
		alpineMirror, alpineBranch, arch, alpineRelease, arch)
}

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

// EnsureFlashChroot ensures a minimal Alpine chroot with android-tools
// (fastboot) + heimdall installed, and returns its root. Idempotent: returns
// immediately once both tools are present.
func EnsureFlashChroot(b *builder.Builder) (string, error) {
	host := builder.HostArchString()
	root := flashChrootDir(b)

	if fileExists(filepath.Join(root, "usr", "bin", "fastboot")) &&
		fileExists(filepath.Join(root, "usr", "bin", "heimdall")) {
		return root, nil
	}

	// 1. Lay down the Alpine base from the minirootfs tarball (~3 MB) unless
	//    the chroot already is one.
	if !fileExists(filepath.Join(root, "etc", "alpine-release")) {
		runner.Logln("Provisioning minimal Alpine flash chroot...")
		tarball, err := b.Download(alpineMinirootfsURL(host), "")
		if err != nil {
			return "", fmt.Errorf("downloading alpine minirootfs: %w", err)
		}
		if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", root)); err != nil {
			return "", fmt.Errorf("creating flash chroot dir: %w", err)
		}
		if err := runner.RunCmd(exec.Command("sudo", "tar", "-xzf", tarball, "-C", root)); err != nil {
			return "", fmt.Errorf("extracting alpine minirootfs: %w", err)
		}
	}

	// 2. DNS + main/community repos so apk can resolve the tools (heimdall is
	//    in community).
	if err := runner.RunCmd(exec.Command("sudo", "cp", "/etc/resolv.conf", filepath.Join(root, "etc", "resolv.conf"))); err != nil {
		return "", fmt.Errorf("seeding resolv.conf: %w", err)
	}
	repos := fmt.Sprintf("%s/%s/main\n%s/%s/community\n", alpineMirror, alpineBranch, alpineMirror, alpineBranch)
	if err := writeChrootFile(root, filepath.Join("etc", "apk", "repositories"), repos); err != nil {
		return "", fmt.Errorf("writing apk repositories: %w", err)
	}

	// 3. Install the flash tools. apk ships in the minirootfs, so this runs
	//    inside the chroot (host-arch, no qemu) — no host apk required.
	runner.Logln("Installing flash tools (android-tools, heimdall) into Alpine flash chroot...")
	// Absolute paths inside the chroot: sudo's secure_path doesn't carry /sbin,
	// so `chroot root apk` fails to resolve via PATH.
	add := exec.Command("sudo", "chroot", root, "/sbin/apk", "add", "--no-cache", "android-tools", "heimdall")
	add.Stdout = runner.LogWriter()
	add.Stderr = runner.LogWriter()
	if err := runner.RunCmd(add); err != nil {
		return "", fmt.Errorf("installing flash tools via apk: %w", err)
	}
	return root, nil
}

// writeChrootFile writes content to <root>/<rel> as root (the chroot tree is
// root-owned), creating parent dirs.
func writeChrootFile(root, rel, content string) error {
	target := filepath.Join(root, rel)
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", filepath.Dir(target))); err != nil {
		return err
	}
	cmd := exec.Command("sudo", "tee", target)
	cmd.Stdin = strings.NewReader(content)
	cmd.Stdout = io.Discard
	return runner.RunCmd(cmd)
}

// mountUSB bind-mounts the host USB bus into the chroot so fastboot/heimdall
// (run as root inside) can see plugged-in devices. Returns an unmount cleanup.
//
// A bind mount of /dev/bus/usb is a point-in-time snapshot of the bus, so a
// device plugged in AFTER an earlier bind won't appear. We therefore re-bind
// fresh on every call: drop any existing mount (lazily, in case a tool is
// holding a node) and bind the current bus. This is what makes detection see
// a device the user plugs in mid-poll.
func mountUSB(root string) (func(), error) {
	target := filepath.Join(root, "dev", "bus", "usb")
	if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", target)); err != nil {
		return nil, fmt.Errorf("creating usb mount point: %w", err)
	}
	if isMounted(target) {
		_ = runner.RunCmd(exec.Command("sudo", "umount", "-l", target))
	}
	if err := runner.RunCmd(exec.Command("sudo", "mount", "--bind", "/dev/bus/usb", target)); err != nil {
		return nil, fmt.Errorf("bind-mounting /dev/bus/usb: %w", err)
	}
	return func() { _ = runner.RunCmd(exec.Command("sudo", "umount", "-l", target)) }, nil
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
		// is in Download Mode, non-zero otherwise. Absolute path: the chroot
		// exec doesn't resolve /usr/bin via PATH under sudo.
		cmd := exec.Command("sudo", "chroot", root, "/usr/bin/heimdall", "detect")
		cmd.Stdout = &out
		cmd.Stderr = &out
		_ = cmd.Run()
		if strings.Contains(out.String(), "Device detected") {
			return []string{"download-mode"}, nil
		}
		return nil, nil
	}

	cmd := exec.Command("sudo", "chroot", root, "/usr/bin/fastboot", "devices")
	cmd.Stdout = &out
	cmd.Stderr = &out
	if err := cmd.Run(); err != nil {
		runner.Logf("flash: fastboot devices failed: %v (%s)\n", err, strings.TrimSpace(out.String()))
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
	// Log the probe so detection is visible in the flash:log stream — quiet
	// when nothing's connected (the common polling case), louder on a hit.
	if len(serials) > 0 {
		runner.Logf("flash: fastboot detected %d device(s): %s\n", len(serials), strings.Join(serials, ", "))
	} else if raw := strings.TrimSpace(out.String()); raw != "" {
		runner.Logf("flash: fastboot devices (no match): %q\n", raw)
	}
	return serials, nil
}
