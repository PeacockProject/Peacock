package installer

import (
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
)

// defaultTargetMount is where RunInstall mounts the freshly-formatted
// root partition. Exposed as a const so tests can reference it.
const defaultTargetMount = "/mnt/peacock-target"

// RunInstall is the orchestrator. It drives every phase in order, emitting
// Progress events to progress (if non-nil). On any error it best-effort
// tears down the mounts and binds it set up, then returns the wrapped
// error.
//
// Phase order:
//  1. validate config
//  2. PhasePartitioning → CreateLayout
//  3. PhaseFormatting → FormatPartitions
//  4. mount root at /mnt/peacock-target, boot at /mnt/peacock-target/boot,
//     bind /proc /sys /dev
//  5. PhaseCopySystem → CopyLiveRootfs (streams progress)
//  6. PhaseBootloader → InstallBootloader
//  7. PhaseUserAndConfig → CreateUser + WriteFstab + SetHostname + SetLocale
//  8. PhaseFinishing → sync, unmount binds, unmount boot, unmount root
//  9. final Progress{Percent: 100, Message: "installation complete"}
func RunInstall(ctx context.Context, cfg Config, progress chan<- Progress) (err error) {
	if err := cfg.Validate(); err != nil {
		return err
	}
	if cfg.BootloaderMode == "" {
		cfg.BootloaderMode = DetectBootMode()
	}

	layout := DefaultLayout(cfg.BootloaderMode)
	parts := PartitionPaths{
		BootDev: PartitionNode(cfg.TargetDiskNode, 1),
		RootDev: PartitionNode(cfg.TargetDiskNode, 2),
	}

	emit := func(p Progress) {
		if progress == nil {
			return
		}
		// Phase-boundary events are blocking — we want the GUI to see them
		// even if it's been slow consuming the rsync tick stream.
		select {
		case progress <- p:
		case <-ctx.Done():
		}
	}

	if err := ctx.Err(); err != nil {
		return err
	}
	emit(Progress{Phase: PhasePartitioning, Percent: 0, Message: "creating partition table"})
	if err := CreateLayout(ctx, cfg.TargetDiskNode, layout); err != nil {
		return fmt.Errorf("partitioning: %w", err)
	}
	emit(Progress{Phase: PhasePartitioning, Percent: 100, Message: "partition table created"})

	if err := ctx.Err(); err != nil {
		return err
	}
	emit(Progress{Phase: PhaseFormatting, Percent: 0, Message: "formatting partitions"})
	if err := FormatPartitions(ctx, parts, layout, "ROOT"); err != nil {
		return fmt.Errorf("formatting: %w", err)
	}
	emit(Progress{Phase: PhaseFormatting, Percent: 100, Message: "filesystems ready"})

	// Mount the target rootfs and the boot partition. After this point we
	// own a stack of mounts that must be unwound on every exit path; we
	// defer the cleanup so context-cancel mid-rsync still tidies up.
	if err := ctx.Err(); err != nil {
		return err
	}
	target := defaultTargetMount
	if err := os.MkdirAll(target, 0o755); err != nil {
		return fmt.Errorf("mkdir %s: %w", target, err)
	}
	if err := mount(parts.RootDev, target, "ext4", 0, ""); err != nil {
		return fmt.Errorf("mount root: %w", err)
	}
	defer func() {
		// Outer-most cleanup: unmount the rootfs. Logged but not returned
		// — if RunInstall already failed we don't want to clobber the
		// real error.
		if uerr := unmount(target); uerr != nil {
			logf(PhaseFinishing, "umount %s failed: %v", target, uerr)
		}
		_ = os.Remove(target)
	}()

	bootMount := filepath.Join(target, "boot")
	if err := os.MkdirAll(bootMount, 0o755); err != nil {
		return fmt.Errorf("mkdir %s: %w", bootMount, err)
	}
	bootFSKind := "vfat"
	if layout.BootFS == "ext2" {
		bootFSKind = "ext2"
	}
	if err := mount(parts.BootDev, bootMount, bootFSKind, 0, ""); err != nil {
		return fmt.Errorf("mount boot: %w", err)
	}
	defer func() {
		if uerr := unmount(bootMount); uerr != nil {
			logf(PhaseFinishing, "umount %s failed: %v", bootMount, uerr)
		}
	}()

	if err := ctx.Err(); err != nil {
		return err
	}
	emit(Progress{Phase: PhaseCopySystem, Percent: 0, Message: "copying live system"})
	copyOpts := CopyOptions{Source: cfg.SourceRoot, Target: target}
	if err := CopyLiveRootfs(ctx, copyOpts, progress); err != nil {
		return fmt.Errorf("copy: %w", err)
	}

	// Bind /proc /sys /dev into the target for chroot operations below.
	if err := bindSystemMounts(target); err != nil {
		return fmt.Errorf("bind /proc /sys /dev: %w", err)
	}
	defer unbindSystemMounts(target)

	if err := ctx.Err(); err != nil {
		return err
	}
	emit(Progress{Phase: PhaseBootloader, Percent: 0, Message: "installing bootloader"})
	if err := InstallBootloader(ctx, BootloaderOpts{
		Mode:       cfg.BootloaderMode,
		TargetRoot: target,
		BootDev:    parts.BootDev,
		DiskDev:    cfg.TargetDiskNode,
	}); err != nil {
		return fmt.Errorf("bootloader: %w", err)
	}
	emit(Progress{Phase: PhaseBootloader, Percent: 100, Message: "bootloader installed"})

	if err := ctx.Err(); err != nil {
		return err
	}
	emit(Progress{Phase: PhaseUserAndConfig, Percent: 0, Message: "configuring system"})
	if err := CreateUser(ctx, target, cfg.User); err != nil {
		return fmt.Errorf("user: %w", err)
	}
	if err := WriteFstab(ctx, target, parts); err != nil {
		return fmt.Errorf("fstab: %w", err)
	}
	if err := SetHostname(ctx, target, cfg.Hostname); err != nil {
		return fmt.Errorf("hostname: %w", err)
	}
	if err := SetLocale(ctx, target, cfg.Locale, cfg.Keymap, cfg.Timezone); err != nil {
		return fmt.Errorf("locale: %w", err)
	}
	emit(Progress{Phase: PhaseUserAndConfig, Percent: 100, Message: "system configured"})

	emit(Progress{Phase: PhaseFinishing, Percent: 50, Message: "syncing"})
	// sync is best-effort.
	if err := exec.CommandContext(ctx, "sync").Run(); err != nil {
		logf(PhaseFinishing, "sync returned %v (continuing)", err)
	}
	emit(Progress{Phase: PhaseFinishing, Percent: 100, Message: "installation complete"})
	return nil
}

// bindSystemMounts bind-mounts /proc /sys /dev /dev/pts into target so
// chroot operations (grub-install, useradd, chpasswd, locale-gen) see a
// working /dev/random, runtime state, etc.
//
// On error mid-sequence, the binds already established are torn down by
// the caller's defer of unbindSystemMounts.
func bindSystemMounts(target string) error {
	pairs := []struct {
		src, dst, fs string
		flags        uintptr
	}{
		{"/proc", filepath.Join(target, "proc"), "proc", 0},
		{"/sys", filepath.Join(target, "sys"), "sysfs", 0},
		{"/dev", filepath.Join(target, "dev"), "", syscall.MS_BIND | syscall.MS_REC},
	}
	for _, p := range pairs {
		if err := os.MkdirAll(p.dst, 0o755); err != nil {
			return err
		}
		if p.flags&syscall.MS_BIND != 0 {
			if err := syscall.Mount(p.src, p.dst, "", p.flags, ""); err != nil {
				return fmt.Errorf("bind %s → %s: %w", p.src, p.dst, err)
			}
		} else {
			if err := syscall.Mount(p.src, p.dst, p.fs, 0, ""); err != nil {
				return fmt.Errorf("mount %s on %s: %w", p.fs, p.dst, err)
			}
		}
	}
	return nil
}

// unbindSystemMounts is the inverse of bindSystemMounts. Best-effort —
// failures are logged but never propagated; we'd rather surface the
// original error from RunInstall.
func unbindSystemMounts(target string) {
	for _, p := range []string{
		filepath.Join(target, "dev", "pts"),
		filepath.Join(target, "dev"),
		filepath.Join(target, "sys"),
		filepath.Join(target, "proc"),
	} {
		if err := syscall.Unmount(p, syscall.MNT_DETACH); err != nil {
			if !errors.Is(err, syscall.EINVAL) && !errors.Is(err, syscall.ENOENT) {
				logf(PhaseFinishing, "umount %s: %v", p, err)
			}
		}
	}
}

// mount is a thin wrapper around syscall.Mount with a friendlier error.
// We avoid runner shellout here because Mount is a syscall that doesn't
// benefit from log fanout and we don't want subprocess overhead during
// teardown paths.
func mount(src, dst, fs string, flags uintptr, data string) error {
	if err := syscall.Mount(src, dst, fs, flags, data); err != nil {
		return fmt.Errorf("mount(%s, %s, %s): %w", src, dst, fs, err)
	}
	return nil
}

// unmount detaches dst lazily, which is appropriate for teardown — we'd
// rather succeed than block on a busy mount.
func unmount(dst string) error {
	return syscall.Unmount(dst, syscall.MNT_DETACH)
}
