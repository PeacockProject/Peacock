package installer

import (
	"context"
	"fmt"
	"os/exec"
	"strconv"
	"strings"
)

// CopyOptions configures CopyLiveRootfs.
//
// Source defaults to "/run/live" when empty. Target is the mount-point of
// the freshly-formatted root partition (e.g. "/mnt/peacock-target").
// Excludes overrides the default exclude set when non-nil; pass an empty
// slice to deliberately copy *everything* (almost never what you want).
type CopyOptions struct {
	Source   string
	Target   string
	Excludes []string
}

// DefaultRsyncExcludes is the canonical set of paths rsync should skip
// when imaging a live system onto a target disk. These are kernel /
// runtime / live-overlay paths that must not be persisted.
//
// PUNT: this list is good enough for a Debian-flavoured live ISO with
// live-boot overlay. Arch-flavoured ISOs that use airootfs may also want
// to exclude /var/lib/pacman/sync and /var/cache/pacman/pkg — left as a
// follow-up once the actual peacockos live ISO recipe lands.
var DefaultRsyncExcludes = []string{
	"/proc/*",
	"/sys/*",
	"/dev/*",
	"/run/*",
	"/tmp/*",
	"/mnt/*",
	"/media/*",
	"/lost+found",
	"/var/cache/apt/archives/*.deb",
	"/var/lib/dhcp/*",
	// live-boot artefacts
	"/lib/live/*",
	"/usr/lib/live/*",
}

// CopyLiveRootfs rsyncs the live system to the target rootfs and streams
// progress events to progress (if non-nil). Cancellation propagates via
// ctx — rsync gets SIGTERM.
//
// Flags used:
//
//	-a    archive (preserve times, perms, symlinks, etc.)
//	-A    ACLs
//	-X    xattrs
//	-H    hardlinks
//	-x    don't cross filesystem boundaries (live overlay safety)
//	--info=progress2 — single global progress line we can parse
//	--no-inc-recursive — needed so progress2 knows the total upfront
func CopyLiveRootfs(ctx context.Context, opts CopyOptions, progress chan<- Progress) error {
	if opts.Source == "" {
		opts.Source = "/run/live"
	}
	if opts.Target == "" {
		return fmt.Errorf("installer: CopyLiveRootfs: Target is required")
	}
	excludes := opts.Excludes
	if excludes == nil {
		excludes = DefaultRsyncExcludes
	}

	src := strings.TrimRight(opts.Source, "/") + "/"
	dst := strings.TrimRight(opts.Target, "/") + "/"

	args := []string{"-aAXHx", "--info=progress2", "--no-inc-recursive"}
	for _, e := range excludes {
		args = append(args, "--exclude="+e)
	}
	args = append(args, src, dst)

	cmd := exec.CommandContext(ctx, "rsync", args...)

	emit := func(p Progress) {
		if progress == nil {
			return
		}
		select {
		case progress <- p:
		case <-ctx.Done():
		default:
			// Drop events when the consumer is slow — rsync emits many.
			// Critical phase-boundary events are sent via the orchestrator
			// with a blocking send instead.
		}
	}

	emit(Progress{Phase: PhaseCopySystem, Percent: 0, Message: "copying live system to target"})

	sink := func(line string) {
		if pct, ok := parseRsyncProgress(line); ok {
			emit(Progress{
				Phase:   PhaseCopySystem,
				Percent: pct,
				Message: "copying live system to target",
				LogLine: line,
			})
			return
		}
		// Forward other lines as untimed log lines (file paths, warnings).
		emit(Progress{Phase: PhaseCopySystem, LogLine: line})
	}

	if err := runTaggedCmd(ctx, PhaseCopySystem, cmd, sink); err != nil {
		return fmt.Errorf("rsync: %w", err)
	}
	emit(Progress{Phase: PhaseCopySystem, Percent: 100, Message: "copy complete"})
	return nil
}

// parseRsyncProgress extracts the percent value from a rsync --info=progress2
// line. The format looks like:
//
//	832,512  62%   12.34MB/s    0:00:15 (xfr#42, to-chk=12/96)
//
// We grab the first token ending in '%'. Returns (0,false) when no percent
// is present.
func parseRsyncProgress(line string) (int, bool) {
	for _, tok := range strings.Fields(line) {
		if !strings.HasSuffix(tok, "%") {
			continue
		}
		val := strings.TrimSuffix(tok, "%")
		n, err := strconv.Atoi(val)
		if err != nil {
			continue
		}
		if n < 0 {
			n = 0
		}
		if n > 100 {
			n = 100
		}
		return n, true
	}
	return 0, false
}
