package installer

import (
	"context"
	"fmt"
	"os"
	"strings"
)

// Layout describes the partition table we lay down on a freshly-erased
// target disk. For v0 we use a fixed two-partition scheme: boot/ESP first,
// then a single root partition filling the remainder.
//
// arm devices typically want a FAT32 ESP regardless of bootloader; that
// case is handled by callers picking BootFS explicitly. Until the
// installer grows a port-specific layout map, x86 grub-UEFI gets vfat ESP
// and x86 extlinux/BIOS gets ext2 /boot.
type Layout struct {
	BootMB    int    // size of the first partition in MB
	BootFS    string // "vfat" or "ext2"
	RootStart string // human path "<bootMB>MiB"
	RootEnd   string // "100%"
	UseGPT    bool
}

// DefaultLayout returns the canonical layout for the given bootMode. The
// caller must already know whether the platform is UEFI or BIOS — the
// helper does not probe.
//
//   - bootMode == "grub":     GPT + 512 MB vfat ESP + ext4 root
//   - bootMode == "extlinux": MBR + 256 MB ext2 /boot + ext4 root
//   - bootMode == "":         autodetected externally; defaults to grub layout
func DefaultLayout(bootMode string) Layout {
	switch bootMode {
	case "extlinux":
		return Layout{
			BootMB:    256,
			BootFS:    "ext2",
			RootStart: "257MiB", // 1MiB alignment slack
			RootEnd:   "100%",
			UseGPT:    false,
		}
	default: // grub or empty
		return Layout{
			BootMB:    512,
			BootFS:    "vfat",
			RootStart: "513MiB",
			RootEnd:   "100%",
			UseGPT:    true,
		}
	}
}

// DetectBootMode returns "grub" when /sys/firmware/efi/efivars exists,
// "extlinux" otherwise. Exposed so RunInstall can fill in
// Config.BootloaderMode when the caller leaves it blank.
func DetectBootMode() string {
	if _, err := os.Stat("/sys/firmware/efi/efivars"); err == nil {
		return "grub"
	}
	return "extlinux"
}

// CreateLayout wipes the existing partition table on disk and writes the
// two-partition scheme described by layout using parted. Destructive.
//
// Sequence:
//  1. wipefs -a <disk>         — clear any existing PMBR / GPT signatures
//  2. parted mklabel gpt|msdos
//  3. parted mkpart boot ...   — sized per layout.BootMB
//  4. parted mkpart root ...   — fills the rest
//  5. parted set <n> esp on   — for the GPT/grub path
//  6. partprobe <disk>         — kick the kernel into re-reading the table
func CreateLayout(ctx context.Context, disk string, layout Layout) error {
	if !strings.HasPrefix(disk, "/dev/") {
		return fmt.Errorf("installer: CreateLayout: disk %q must be a /dev/ path", disk)
	}
	logf(PhasePartitioning, "wiping signatures on %s", disk)
	if err := runTagged(ctx, PhasePartitioning, "wipefs", "-a", disk); err != nil {
		return fmt.Errorf("wipefs %s: %w", disk, err)
	}

	label := "msdos"
	if layout.UseGPT {
		label = "gpt"
	}
	logf(PhasePartitioning, "creating %s table on %s", label, disk)
	if err := runTagged(ctx, PhasePartitioning,
		"parted", "-s", "-a", "optimal", disk, "mklabel", label); err != nil {
		return fmt.Errorf("parted mklabel %s on %s: %w", label, disk, err)
	}

	// Boot partition: 1MiB..(1+BootMB)MiB
	bootEnd := fmt.Sprintf("%dMiB", 1+layout.BootMB)
	bootFS := "fat32"
	if layout.BootFS == "ext2" {
		bootFS = "ext2"
	}
	if err := runTagged(ctx, PhasePartitioning,
		"parted", "-s", "-a", "optimal", disk,
		"mkpart", "primary", bootFS, "1MiB", bootEnd); err != nil {
		return fmt.Errorf("parted mkpart boot on %s: %w", disk, err)
	}

	if err := runTagged(ctx, PhasePartitioning,
		"parted", "-s", "-a", "optimal", disk,
		"mkpart", "primary", "ext4", layout.RootStart, layout.RootEnd); err != nil {
		return fmt.Errorf("parted mkpart root on %s: %w", disk, err)
	}

	if layout.UseGPT {
		// Mark the ESP so firmware finds it.
		if err := runTagged(ctx, PhasePartitioning,
			"parted", "-s", disk, "set", "1", "esp", "on"); err != nil {
			return fmt.Errorf("parted set esp on %s: %w", disk, err)
		}
	} else {
		// BIOS: make partition 1 bootable for SYSLINUX MBR.
		if err := runTagged(ctx, PhasePartitioning,
			"parted", "-s", disk, "set", "1", "boot", "on"); err != nil {
			return fmt.Errorf("parted set boot on %s: %w", disk, err)
		}
	}

	// Best-effort kernel re-read. partprobe failure isn't fatal — udev will
	// usually settle in time for mkfs to find the partitions.
	if err := runTagged(ctx, PhasePartitioning, "partprobe", disk); err != nil {
		logf(PhasePartitioning, "partprobe returned %v (continuing)", err)
	}
	return nil
}

// PartitionNode maps a disk node + partition index → the partition device
// node. e.g. ("/dev/sda", 1) → "/dev/sda1"; ("/dev/mmcblk0", 2) →
// "/dev/mmcblk0p2".
func PartitionNode(disk string, idx int) string {
	if strings.HasPrefix(disk, "/dev/nvme") || strings.HasPrefix(disk, "/dev/mmcblk") ||
		strings.HasPrefix(disk, "/dev/loop") {
		return fmt.Sprintf("%sp%d", disk, idx)
	}
	return fmt.Sprintf("%s%d", disk, idx)
}
