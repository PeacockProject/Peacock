package installer

import (
	"context"
	"fmt"
	"strings"
)

// PartitionPaths names the partition device nodes resulting from
// CreateLayout. Tracked as a struct so the orchestrator can pass it
// straight to FormatPartitions, mount helpers, fstab, and bootloader
// install without recomputing.
type PartitionPaths struct {
	BootDev string
	RootDev string
}

// FormatPartitions writes filesystems onto paths.BootDev + paths.RootDev
// matching layout.BootFS (vfat or ext2). The root is always ext4, labelled
// rootLabel; the boot fs gets a stable label so fstab can `LABEL=…` mount
// it across kernel device renumberings.
func FormatPartitions(ctx context.Context, paths PartitionPaths, layout Layout, rootLabel string) error {
	if !strings.HasPrefix(paths.BootDev, "/dev/") {
		return fmt.Errorf("installer: FormatPartitions: BootDev %q must be /dev/ path", paths.BootDev)
	}
	if !strings.HasPrefix(paths.RootDev, "/dev/") {
		return fmt.Errorf("installer: FormatPartitions: RootDev %q must be /dev/ path", paths.RootDev)
	}
	if rootLabel == "" {
		rootLabel = "ROOT"
	}

	switch layout.BootFS {
	case "vfat":
		// FAT32, label "PEACOCK_ESP". mkfs.vfat -F 32 -n LABEL.
		// -F 32 forces FAT32 even on small partitions.
		if err := runTagged(ctx, PhaseFormatting,
			"mkfs.vfat", "-F", "32", "-n", "PEACOCK_ESP", paths.BootDev); err != nil {
			return fmt.Errorf("mkfs.vfat %s: %w", paths.BootDev, err)
		}
	case "ext2":
		if err := runTagged(ctx, PhaseFormatting,
			"mkfs.ext2", "-F", "-L", "PEACOCK_BOOT", paths.BootDev); err != nil {
			return fmt.Errorf("mkfs.ext2 %s: %w", paths.BootDev, err)
		}
	default:
		return fmt.Errorf("installer: unsupported BootFS %q", layout.BootFS)
	}

	if err := runTagged(ctx, PhaseFormatting,
		"mkfs.ext4", "-F", "-L", rootLabel, paths.RootDev); err != nil {
		return fmt.Errorf("mkfs.ext4 %s: %w", paths.RootDev, err)
	}
	return nil
}
