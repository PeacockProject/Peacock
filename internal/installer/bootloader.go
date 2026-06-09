package installer

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// BootloaderOpts is the input for InstallBootloader.
//
// Mode:       "grub" or "extlinux".
// TargetRoot: mount point of the new root (e.g. "/mnt/peacock-target").
// BootDev:    device node of the boot partition (e.g. "/dev/sda1").
// DiskDev:    whole-disk node for MBR install (e.g. "/dev/sda").
type BootloaderOpts struct {
	Mode       string
	TargetRoot string
	BootDev    string
	DiskDev    string
}

// InstallBootloader installs grub-uefi or extlinux+MBR onto the target.
// Assumes the boot partition is already mounted at <TargetRoot>/boot and
// the /proc /sys /dev binds into TargetRoot have already been established
// by the orchestrator (RunInstall does this).
//
// PUNT: grub vs grub2 binary names. Debian/Ubuntu ship `grub-install`;
// Fedora/RHEL family ships `grub2-install`. Today we shell out to the
// binary present inside the target chroot, so as long as the live ISO is
// built from a Debian-flavoured base this works. A follow-up should detect
// `grub2-install` and pick the right name. Arch ships `grub-install`.
func InstallBootloader(ctx context.Context, opts BootloaderOpts) error {
	if opts.TargetRoot == "" {
		return fmt.Errorf("installer: InstallBootloader: TargetRoot required")
	}
	switch opts.Mode {
	case "grub":
		return installGrub(ctx, opts)
	case "extlinux":
		return installExtlinux(ctx, opts)
	default:
		return fmt.Errorf("installer: unsupported bootloader mode %q", opts.Mode)
	}
}

func installGrub(ctx context.Context, opts BootloaderOpts) error {
	if opts.BootDev == "" {
		return fmt.Errorf("installer: grub install requires BootDev")
	}
	// grub-install --target=x86_64-efi --efi-directory=/boot/efi
	//   --bootloader-id=PeacockOS --recheck
	// Run inside chroot so the target's grub package is used.
	// We assume <TargetRoot>/boot is already where the ESP is mounted.
	args := []string{
		"chroot", opts.TargetRoot,
		"grub-install",
		"--target=x86_64-efi",
		"--efi-directory=/boot",
		"--bootloader-id=PeacockOS",
		"--recheck",
		"--no-nvram", // skip efibootmgr writes; the live ISO firmware path is messy
	}
	if err := runTagged(ctx, PhaseBootloader, args[0], args[1:]...); err != nil {
		return fmt.Errorf("grub-install: %w", err)
	}

	cfgArgs := []string{
		"chroot", opts.TargetRoot,
		"grub-mkconfig", "-o", "/boot/grub/grub.cfg",
	}
	if err := runTagged(ctx, PhaseBootloader, cfgArgs[0], cfgArgs[1:]...); err != nil {
		return fmt.Errorf("grub-mkconfig: %w", err)
	}
	return nil
}

func installExtlinux(ctx context.Context, opts BootloaderOpts) error {
	if opts.DiskDev == "" {
		return fmt.Errorf("installer: extlinux install requires DiskDev")
	}

	extlinuxDir := filepath.Join(opts.TargetRoot, "boot", "extlinux")
	if err := os.MkdirAll(extlinuxDir, 0o755); err != nil {
		return fmt.Errorf("installer: mkdir %s: %w", extlinuxDir, err)
	}

	cfgPath := filepath.Join(extlinuxDir, "extlinux.conf")
	cfg, err := buildExtlinuxConf(opts.TargetRoot)
	if err != nil {
		return fmt.Errorf("installer: build extlinux.conf: %w", err)
	}
	if err := os.WriteFile(cfgPath, []byte(cfg), 0o644); err != nil {
		return fmt.Errorf("installer: write %s: %w", cfgPath, err)
	}

	if err := runTagged(ctx, PhaseBootloader, "extlinux", "--install", extlinuxDir); err != nil {
		return fmt.Errorf("extlinux --install: %w", err)
	}

	// Write the SYSLINUX MBR boot code to the disk's first sector.
	mbr := findSyslinuxMBR(opts.TargetRoot)
	if mbr == "" {
		return fmt.Errorf("installer: could not locate syslinux mbr.bin in target")
	}
	dd := exec.CommandContext(ctx, "dd",
		"if="+mbr,
		"of="+opts.DiskDev,
		"bs=440", "count=1", "conv=notrunc")
	if err := runTaggedCmd(ctx, PhaseBootloader, dd, nil); err != nil {
		return fmt.Errorf("dd mbr.bin → %s: %w", opts.DiskDev, err)
	}
	return nil
}

// buildExtlinuxConf scans <target>/boot for vmlinuz-* / initrd.img-* (or
// initramfs-*) and emits a minimal extlinux.conf pointing at the first
// match. Picks the lexically-greatest filename so the newest kernel wins.
func buildExtlinuxConf(targetRoot string) (string, error) {
	bootDir := filepath.Join(targetRoot, "boot")
	entries, err := os.ReadDir(bootDir)
	if err != nil {
		return "", fmt.Errorf("read %s: %w", bootDir, err)
	}

	var kernel, initrd string
	for _, e := range entries {
		name := e.Name()
		switch {
		case strings.HasPrefix(name, "vmlinuz-"):
			if name > kernel {
				kernel = name
			}
		case strings.HasPrefix(name, "initrd.img-") || strings.HasPrefix(name, "initramfs-"):
			if name > initrd {
				initrd = name
			}
		}
	}
	if kernel == "" {
		return "", fmt.Errorf("no vmlinuz-* in %s", bootDir)
	}

	var sb strings.Builder
	sb.WriteString("DEFAULT peacock\n")
	sb.WriteString("PROMPT 0\n")
	sb.WriteString("TIMEOUT 30\n\n")
	sb.WriteString("LABEL peacock\n")
	sb.WriteString("    MENU LABEL PeacockOS\n")
	sb.WriteString("    LINUX /" + kernel + "\n")
	if initrd != "" {
		sb.WriteString("    INITRD /" + initrd + "\n")
	}
	sb.WriteString("    APPEND root=LABEL=ROOT rw quiet\n")
	return sb.String(), nil
}

// findSyslinuxMBR looks for mbr.bin in the usual locations inside the
// target rootfs. Returns "" when none is present.
func findSyslinuxMBR(targetRoot string) string {
	candidates := []string{
		"usr/lib/syslinux/bios/mbr.bin",
		"usr/lib/syslinux/mbr/mbr.bin",
		"usr/share/syslinux/mbr.bin",
		"usr/lib/EXTLINUX/mbr.bin",
	}
	for _, c := range candidates {
		full := filepath.Join(targetRoot, c)
		if _, err := os.Stat(full); err == nil {
			return full
		}
	}
	return ""
}
