package mkinitfs

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"text/template"

	"peacock/internal/runner"
)

// InitConfig holds configuration for the init script validation
type InitConfig struct {
	InitSystem        string // "openrc" or "systemd"
	RootLabel         string // Filesystem label for root partition (e.g., "ROOT")
	BusyboxPath       string // Path to static busybox binary
	Resize2fsPath     string // Path to resize2fs binary (optional, will try to find if empty)
	SplashPath        string // Path to peacock-splash binary (optional)
	RefresherPath     string // Path to msm-fb-refresher binary (optional)
	Architecture      string // Target arch (e.g., "armv7h", "aarch64", "x86_64")
	DeviceName        string // Device codename (e.g., "samsung-jflte")
	EnableS4CameraLED bool   // Enable S4-specific camera LED debug flashes in initramfs
	// UtilLinuxBuildDir points at the staged util-linux port build directory
	// (sbin/, bin/, usr/bin/, lib/, usr/lib/). When set, the initramfs builder
	// harvests losetup/partx/blkid/lsblk + shared libs from here. Falls back to
	// a no-op when empty (e.g. legacy callers or partial builds).
	UtilLinuxBuildDir string
	// Lvm2BuildDir points at the staged lvm2 port build directory which
	// provides sbin/dmsetup and libdevmapper. When set, the initramfs builder
	// prefers this over host paths for the dmsetup binary + its lib search.
	Lvm2BuildDir string
}

// Compile-time toggle for S4 camera LED debug flashes in initramfs.
// Keep disabled by default; set to true only when explicitly debugging boot stages.
const enableS4CameraLED = false

const initScriptTemplate = `#!/bin/busybox ash

# Continue on errors - many commands will fail in early boot and that's OK
set +e

# Install busybox symlinks (CRITICAL for kernel to run this script!)
/bin/busybox --install -s

# Runtime toolchain from PRP sync (fdisk/partx/dmsetup) needs shared libs early.
export LD_LIBRARY_PATH=/lib:/usr/lib:/sbin:/usr/sbin

# Ensure essential mount points exist early
mkdir -p /proc /sys /dev /run /tmp /etc /usr /lib

# Optional persistent log sink (prefer CACHE partition), best-effort.
BOOTLOG_MNT="/run/peacock-bootlog"
BOOTLOG_DEV=""
BOOTLOG_FILE=""
bootlog_try_mount_dev() {
    local dev="$1"
    [ -n "$dev" ] || return 1
    [ -b "$dev" ] || return 1
    [ -n "$BOOTLOG_FILE" ] && return 0
    /bin/busybox mkdir -p "$BOOTLOG_MNT" 2>/dev/null || true
    /bin/busybox mount -t ext2 -o rw "$dev" "$BOOTLOG_MNT" >/dev/null 2>&1 || \
        /bin/busybox mount -o rw "$dev" "$BOOTLOG_MNT" >/dev/null 2>&1 || return 1
    BOOTLOG_DEV="$dev"
    BOOTLOG_FILE="$BOOTLOG_MNT/peacock-initramfs.log"
    echo "=== peacock initramfs boot $(date +%s) ===" >> "$BOOTLOG_FILE" 2>/dev/null || true
    return 0
}
bootlog_try_label_or_fallback() {
    [ -n "$BOOTLOG_FILE" ] && return 0
    local dev=""
    local uevent=""
    local pn=""
    local node=""
    if /bin/busybox --list 2>/dev/null | /bin/busybox grep -qx timeout; then
        # Prefer a dedicated persistent log partition label first.
        /bin/busybox timeout 1 /bin/busybox findfs "LABEL=PRP_LOG" >/tmp/findfs.bootdev 2>/dev/null || true
        dev="$(/bin/busybox cat /tmp/findfs.bootdev 2>/dev/null || true)"
        bootlog_try_mount_dev "$dev" && return 0
        /bin/busybox timeout 1 /bin/busybox findfs "LABEL=PRP_ROOTFS" >/tmp/findfs.bootdev 2>/dev/null || true
        dev="$(/bin/busybox cat /tmp/findfs.bootdev 2>/dev/null || true)"
        bootlog_try_mount_dev "$dev" && return 0
        /bin/busybox timeout 1 /bin/busybox findfs "LABEL=CACHE" >/tmp/findfs.bootdev 2>/dev/null || true
        dev="$(/bin/busybox cat /tmp/findfs.bootdev 2>/dev/null || true)"
        bootlog_try_mount_dev "$dev" && return 0
        /bin/busybox timeout 1 /bin/busybox findfs "LABEL=BOOT" >/tmp/findfs.bootdev 2>/dev/null || true
        dev="$(/bin/busybox cat /tmp/findfs.bootdev 2>/dev/null || true)"
        /bin/busybox rm -f /tmp/findfs.bootdev >/dev/null 2>&1 || true
        bootlog_try_mount_dev "$dev" && return 0
    fi
    # Prefer Android by-name aliases when present.
    for dev in /dev/block/by-name/cache /dev/block/platform/*/by-name/cache; do
        [ -e "$dev" ] || continue
        dev="$(/bin/busybox readlink -f "$dev" 2>/dev/null || echo "$dev")"
        bootlog_try_mount_dev "$dev" && return 0
    done
    # Resolve by PARTNAME when by-name symlinks are not available.
    for uevent in /sys/class/block/mmcblk0p*/uevent; do
        [ -f "$uevent" ] || continue
        pn="$(/bin/busybox sed -n 's/^PARTNAME=//p' "$uevent" 2>/dev/null || true)"
        case "$pn" in
            cache|CACHE)
                node="${uevent%/uevent}"
                node="${node##*/}"
                dev="/dev/$node"
                bootlog_try_mount_dev "$dev" && return 0
                ;;
        esac
    done
    # Intentionally no hardcoded device-node fallbacks here.
    # Keep this path generic: labels, by-name aliases, and PARTNAME probes only.
    return 1
}
bootlog_try_from_root() {
    [ -n "$BOOTLOG_FILE" ] && return 0
    local root="$1"
    local dev=""
    case "$root" in
        *s1) dev="${root%s1}s0" ;;
        *p2) dev="${root%p2}p1" ;;
    esac
    bootlog_try_mount_dev "$dev" && return 0
    return 1
}
bootlog_close() {
    # Never block handoff on debug log sink teardown.
    if [ -n "$BOOTLOG_FILE" ]; then
        echo "$(date +%s) PEACOCK: closing bootlog sink" >> "$BOOTLOG_FILE" 2>/dev/null || true
    fi
    if [ -n "$BOOTLOG_DEV" ]; then
        (/bin/busybox umount -l "$BOOTLOG_MNT" >/dev/null 2>&1 || true) &
    fi
    BOOTLOG_DEV=""
    BOOTLOG_FILE=""
}

# Logging function - writes to multiple places for debugging
log() {
    local msg="$1"
    local line="$(date +%s) PEACOCK: $msg"
    # Write to kernel log buffer (shows in /proc/last_kmsg)
    echo "<6>PEACOCK: $msg" > /proc/kmsg 2>/dev/null || true
    # Keep framebuffer untouched by default (console writes repaint text over splash).
    if [ "${PEACOCK_INIT_CONSOLE_LOG:-0}" = "1" ]; then
        echo "PEACOCK: $msg" > /dev/console 2>&1 || echo "PEACOCK: $msg" || true
    fi
    # Keep RAM log always.
    echo "$line" >> /tmp/peacock-init.log 2>/dev/null || true
    # Mirror to BOOT log sink when available.
    [ -n "$BOOTLOG_FILE" ] && echo "$line" >> "$BOOTLOG_FILE" 2>/dev/null || true
}

# Helper function to show splash message with debugging
splash() {
    local msg="$1"
    local y="${2:-1}"
    local text_color="${PEACOCK_SPLASH_TEXT_COLOR:-FFFF00}"
    log "SPLASH: $msg"
    # Try framebuffer splash
    if [ -x /bin/peacock-splash ]; then
        local fbdev="${PEACOCK_FBDEV:-}"
        if [ -z "$fbdev" ]; then
            # Prefer primary panel fb0; fallback to fb1
            if [ -c /dev/fb0 ]; then
                fbdev="/dev/fb0"
            elif [ -c /dev/graphics/fb0 ]; then
                fbdev="/dev/graphics/fb0"
            elif [ -c /dev/fb1 ]; then
                fbdev="/dev/fb1"
            elif [ -c /dev/graphics/fb1 ]; then
                fbdev="/dev/graphics/fb1"
            fi
        fi
        # Keep a resolved global FBDEV so later handoff flare can reuse it.
        if [ -z "${FBDEV:-}" ] && [ -n "$fbdev" ]; then
            FBDEV="$fbdev"
        fi
        if [ -n "$fbdev" ]; then
            log "Using framebuffer device: $fbdev"
        fi
        log "Attempting framebuffer splash: $msg"
        /bin/peacock-splash "$msg" "$y" "$fbdev" 000000 noclear logo "text=$text_color" 2>&1 | while read line; do log "peacock-splash: $line"; done || log "peacock-splash failed for: $msg"
    else
        log "peacock-splash not found or not executable"
    fi
}

# LED Debug: Flash camera LED to indicate boot progress
# Usage: flash_led <count> <delay_ms>
{{if .EnableS4CameraLED}}
flash_led() {
    local count="${1:-1}"
    local delay="${2:-200}"
    local led="/sys/devices/platform/i2c-gpio.12/i2c-12/12-0066/max77693-led/leds/leds-sec1/brightness"
    
    # Check if LED exists
    if [ ! -f "$led" ]; then
        log "LED not found at $led"
        return
    fi
    
    log "Flashing LED $count times"
    local i=0
    while [ $i -lt $count ]; do
        echo 63 > "$led" 2>/dev/null || true
        /bin/busybox usleep ${delay}000 || /bin/busybox sleep 1
        echo 0 > "$led" 2>/dev/null || true
        /bin/busybox usleep ${delay}000 || /bin/busybox sleep 1
        i=$((i + 1))
    done
}
{{else}}
flash_led() { :; }
{{end}}

# Mount special filesystems (idempotent; some kernels auto-mount devtmpfs)
/bin/busybox mountpoint -q /proc 2>/dev/null || /bin/busybox mount -t proc proc /proc
/bin/busybox mountpoint -q /sys 2>/dev/null || /bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mountpoint -q /dev 2>/dev/null || /bin/busybox mount -t devtmpfs dev /dev
bootlog_try_label_or_fallback || true

log "=== Peacock Initramfs Starting ==="

# Test framebuffer devices early
log "Checking framebuffer devices..."
ls -la /dev/graphics/ 2>&1 | head -5 | while read line; do log "FB DEV: $line"; done || true
ls -la /dev/fb* 2>&1 | while read line; do log "FB: $line"; done || true

# Wait for fb0 to appear (up to 10s) like pmOS
for i in 1 2 3 4 5 6 7 8 9 10; do
    [ -e /dev/fb0 ] && break
    /bin/busybox sleep 1
done
if [ ! -e /dev/fb0 ]; then
    log "ERROR: /dev/fb0 did not appear after waiting 10 seconds"
fi

# Try to set framebuffer mode if unset
if [ -e /sys/class/graphics/fb0/modes ] && [ -z "$(cat /sys/class/graphics/fb0/mode 2>/dev/null)" ]; then
    fb_mode="$(head -n 1 /sys/class/graphics/fb0/modes 2>/dev/null)"
    if [ -n "$fb_mode" ]; then
        log "Setting framebuffer mode: $fb_mode"
        echo "$fb_mode" > /sys/class/graphics/fb0/mode 2>/dev/null || log "Failed to set fb0 mode"
    fi
fi

# Ensure fb0 is unblanked
if [ -e /sys/class/graphics/fb0/blank ]; then
    echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null || true
fi

# Start MSM framebuffer refresher to keep panel alive
REFRESHER_PID=""
if [ -x /bin/msm-fb-refresher ]; then
    log "Starting msm-fb-refresher --loop"
    /bin/msm-fb-refresher --loop >/dev/null 2>&1 &
    REFRESHER_PID="$!"
fi

# Flash 1x = init started AND filesystems mounted
# (Must be after sysfs mount so LED device exists)
flash_led 1 300

# Check framebuffer again after mounting dev
log "After mounting dev, checking framebuffer..."
ls -la /dev/graphics/ 2>&1 | head -5 | while read line; do log "FB DEV: $line"; done || true
ls -la /dev/fb* 2>&1 | while read line; do log "FB: $line"; done || true

# Test direct framebuffer write if available
if [ -c /dev/fb0 ] || [ -c /dev/graphics/fb0 ]; then
    FB_DEV="/dev/fb0"
    [ -c /dev/graphics/fb0 ] && FB_DEV="/dev/graphics/fb0"
    log "Found framebuffer device: $FB_DEV"
    # Just test if we can open it - actual write test would require knowing the format
    if [ -w "$FB_DEV" ]; then
        log "Framebuffer is writable"
    else
        log "Framebuffer not writable or permission denied"
    fi
else
    log "No framebuffer device found yet"
fi

# Log kernel command line
log "Kernel cmdline: $(cat /proc/cmdline 2>/dev/null || echo 'not available')"

# Allow override of framebuffer device via cmdline: peacock.fb=/dev/fb1
PEACOCK_FBDEV="$(cat /proc/cmdline 2>/dev/null | /bin/busybox sed -n 's/.*peacock.fb=\\([^ ]*\\).*/\\1/p')"
FBDEV="$PEACOCK_FBDEV"
if [ -z "$FBDEV" ]; then
    if [ -c /dev/fb0 ]; then
        FBDEV="/dev/fb0"
    elif [ -c /dev/graphics/fb0 ]; then
        FBDEV="/dev/graphics/fb0"
    elif [ -c /dev/fb1 ]; then
        FBDEV="/dev/fb1"
    elif [ -c /dev/graphics/fb1 ]; then
        FBDEV="/dev/graphics/fb1"
    fi
fi

splash "Peacock Initramfs: Booting..." 1
log "Holding splash for 3 seconds for visibility..."
/bin/busybox sleep 3

# Load modules (simplified)
# for mod in /lib/modules/*/kernel/drivers/*; do insmod $mod; done

# Mount Root (detect by label)
splash "Mounting root by label {{.RootLabel}}..." 2
mkdir -p /new_root
ROOT_DEV=""
find_root_by_label() {
    # On some downstream kernels, plain findfs can block on bad block nodes.
    # Bound runtime and continue with dynamic probing if lookup stalls.
    if /bin/busybox --list 2>/dev/null | /bin/busybox grep -qx timeout; then
        /bin/busybox timeout 2 /bin/busybox findfs "LABEL={{.RootLabel}}" >/tmp/findfs.rootdev 2>/dev/null || true
        ROOT_DEV="$(/bin/busybox cat /tmp/findfs.rootdev 2>/dev/null || true)"
        /bin/busybox rm -f /tmp/findfs.rootdev >/dev/null 2>&1 || true
    else
        # No timeout applet available: skip unbounded findfs to avoid boot hangs.
        ROOT_DEV=""
    fi
    [ -n "$ROOT_DEV" ] && [ ! -b "$ROOT_DEV" ] && ROOT_DEV=""
}

# Try direct label lookup first, but never block boot if findfs stalls.
splash "Root detect: label lookup..." 3
find_root_by_label

# Helpers for dynamic device discovery (no hardcoded partition numbers).
CONTAINERS=""
PROBE_DEVS=""
LOOP_DEVICES=""
append_unique() {
    local var="$1"
    local val="$2"
    [ -n "$val" ] || return 0
    [ -b "$val" ] || return 0
    eval "case \" \${$var} \" in *\" $val \"*) : ;; *) $var=\"\${$var} $val\" ;; esac"
}
resolve_block_dev() {
    local dev="$1"
    local resolved="$dev"
    local t=""
    t="$(/bin/busybox readlink -f "$dev" 2>/dev/null || true)"
    if [ -n "$t" ] && [ -b "$t" ]; then
        resolved="$t"
    fi
    echo "$resolved"
}
add_container_candidates() {
    local dev=""
    local auto_userdata=""
    # Prefer named userdata aliases when available.
    for dev in /dev/block/by-name/userdata /dev/block/platform/*/by-name/userdata; do
        [ -e "$dev" ] || continue
        dev="$(resolve_block_dev "$dev")"
        append_unique CONTAINERS "$dev"
    done
    # Fallback heuristic: choose largest mmc partition from /proc/partitions.
    # This avoids probing every node with dd in early boot, which may stall on some kernels.
    if [ -z "$CONTAINERS" ] && [ -r /proc/partitions ]; then
        auto_userdata="$(
            /bin/busybox awk '
                $4 ~ /^mmcblk[0-9]+p[0-9]+$/ {
                    if ($3 > max) { max=$3; node=$4 }
                }
                END {
                    if (node != "") print "/dev/" node
                }
            ' /proc/partitions 2>/dev/null || true
        )"
        append_unique CONTAINERS "$auto_userdata"
    fi
}
add_probe_candidates_from_container() {
    local base_dev="$1"
    local node=""
    local cand=""
    base_dev="$(resolve_block_dev "$base_dev")"
    node="${base_dev##*/}"
    for cand in \
        "/dev/${node}p1" "/dev/${node}p2" "/dev/${node}s0" "/dev/${node}s1" \
        "/dev/block/${node}p1" "/dev/block/${node}p2" "/dev/block/${node}s0" "/dev/block/${node}s1"; do
        append_unique PROBE_DEVS "$cand"
    done
}
has_busybox_applet() {
    # Check whether the applet exists in this busybox build.
    /bin/busybox --list 2>/dev/null | /bin/busybox grep -qx "$1"
}
attach_loop_partitions() {
    local src="$1"
    local loopdev=""
    local node=""
    local cand=""
    local ptries=0
    [ -b "$src" ] || return 1
    has_busybox_applet losetup || return 1
    loopdev="$(/bin/busybox losetup -f 2>/dev/null || true)"
    [ -n "$loopdev" ] || return 1
    [ -b "$loopdev" ] || return 1
    /bin/busybox losetup -d "$loopdev" >/dev/null 2>&1 || true
    /bin/busybox losetup -P "$loopdev" "$src" >/dev/null 2>&1 || return 1
    LOOP_DEVICES="$LOOP_DEVICES $loopdev"
    node="${loopdev##*/}"
    if has_busybox_applet partprobe; then
        /bin/busybox partprobe "$loopdev" >/dev/null 2>&1 || true
    fi
    if has_busybox_applet blockdev; then
        /bin/busybox blockdev --rereadpt "$loopdev" >/dev/null 2>&1 || true
    fi
    while [ "$ptries" -lt 4 ]; do
        /bin/busybox mdev -s >/dev/null 2>&1 || true
        cand="$(ensure_block_node "${node}p1" 2>/dev/null || true)"
        append_unique PROBE_DEVS "$cand"
        append_unique PROBE_DEVS "/dev/block/${node}p1"
        cand="$(ensure_block_node "${node}p2" 2>/dev/null || true)"
        append_unique PROBE_DEVS "$cand"
        append_unique PROBE_DEVS "/dev/block/${node}p2"
        ptries=$((ptries + 1))
        /bin/busybox sleep 1
    done
    log "loop partition mapping active: $loopdev -> $src"
    return 0
}

find_free_loop() {
    local skip="${1:-}"
    local l=""
    local sys=""
    if has_busybox_applet losetup; then
        l="$(/bin/busybox losetup -f 2>/dev/null || true)"
        if [ -n "$l" ] && [ "$l" != "$skip" ] && [ -b "$l" ]; then
            echo "$l"
            return 0
        fi
    fi
    for sys in /sys/class/block/loop*/loop/backing_file; do
        [ -e "$sys" ] || continue
        l="/dev/${sys%/loop/backing_file}"
        l="${l##*/}"
        l="/dev/$l"
        [ -b "$l" ] || continue
        [ "$l" = "$skip" ] && continue
        if [ ! -s "$sys" ]; then
            echo "$l"
            return 0
        fi
    done
    for l in /dev/loop0 /dev/loop1 /dev/loop2 /dev/loop3 /dev/loop4 /dev/loop5 /dev/loop6 /dev/loop7; do
        [ -b "$l" ] || continue
        [ "$l" = "$skip" ] && continue
        sys="/sys/class/block/${l##*/}/loop/backing_file"
        if [ ! -e "$sys" ] || [ ! -s "$sys" ]; then
            echo "$l"
            return 0
        fi
    done
    return 1
}

ensure_block_node() {
    local node_name="$1"
    local devspec=""
    local maj=""
    local min=""
    [ -n "$node_name" ] || return 1
    [ -b "/dev/$node_name" ] && {
        echo "/dev/$node_name"
        return 0
    }
    devspec="$(/bin/busybox cat "/sys/class/block/$node_name/dev" 2>/dev/null || true)"
    case "$devspec" in
        *:*)
            maj="${devspec%:*}"
            min="${devspec#*:}"
            /bin/busybox mknod "/dev/$node_name" b "$maj" "$min" 2>/dev/null || true
            ;;
    esac
    [ -b "/dev/$node_name" ] && {
        echo "/dev/$node_name"
        return 0
    }
    return 1
}

refresh_nested_candidates() {
    local userdata_dev="$1"
    local tries=0
    local node=""
    local cand=""
    local base_node=""
    base_node="${userdata_dev##*/}"
    while [ "$tries" -lt 4 ]; do
        /bin/busybox mdev -s >/dev/null 2>&1 || true
        for node in "${base_node}p1" "${base_node}p2" "${base_node}s0" "${base_node}s1"; do
            cand="$(ensure_block_node "$node" 2>/dev/null || true)"
            append_unique PROBE_DEVS "$cand"
        done
        tries=$((tries + 1))
        /bin/busybox sleep 1
    done
}

validate_root_candidate() {
    local dev="$1"
    local probe_mnt="/run/peacock-root-probe"
    local has_timeout=0
    [ -b "$dev" ] || return 1
    has_busybox_applet timeout && has_timeout=1
    /bin/busybox mkdir -p "$probe_mnt" 2>/dev/null || true
    if { [ "$has_timeout" -eq 1 ] && /bin/busybox timeout 2 /bin/busybox mount -t ext4 -o ro "$dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 0 ] && /bin/busybox mount -t ext4 -o ro "$dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 1 ] && /bin/busybox timeout 2 /bin/busybox mount -t ext4 -o ro,noload "$dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 0 ] && /bin/busybox mount -t ext4 -o ro,noload "$dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 1 ] && /bin/busybox timeout 2 /bin/busybox mount -t ext2 -o ro "$dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 0 ] && /bin/busybox mount -t ext2 -o ro "$dev" "$probe_mnt" >/dev/null 2>&1; }; then
        /bin/busybox umount "$probe_mnt" >/dev/null 2>&1 || true
        return 0
    fi
    return 1
}

resolve_userdata_dev() {
    local dev=""
    local uevent=""
    local node=""
    local auto_userdata=""
    # Prefer explicit by-name aliases.
    for dev in \
        /dev/block/platform/*/by-name/USERDATA \
        /dev/block/platform/*/by-name/userdata \
        /dev/block/by-name/USERDATA \
        /dev/block/by-name/userdata; do
        [ -e "$dev" ] || continue
        dev="$(resolve_block_dev "$dev")"
        [ -b "$dev" ] && {
            echo "$dev"
            return 0
        }
    done
    # Fallback by PARTNAME from sysfs.
    for uevent in /sys/class/block/mmcblk0p*/uevent; do
        [ -f "$uevent" ] || continue
        if /bin/busybox grep -qi '^PARTNAME=userdata$' "$uevent" 2>/dev/null; then
            node="${uevent%/uevent}"
            node="${node##*/}"
            dev="/dev/$node"
            [ -b "$dev" ] && {
                echo "$dev"
                return 0
            }
        fi
    done
    # Last resort: largest mmc partition.
    auto_userdata="$(
        /bin/busybox awk '
            $4 ~ /^mmcblk[0-9]+p[0-9]+$/ {
                if ($3 > max) { max=$3; node=$4 }
            }
            END {
                if (node != "") print "/dev/" node
            }
        ' /proc/partitions 2>/dev/null || true
    )"
    [ -b "$auto_userdata" ] && {
        echo "$auto_userdata"
        return 0
    }
    return 1
}

# NOTE: setup_prp_like_subparts is an inline copy of the canonical sub-partition
# mount shell. The canonical implementation now ships in the cpio at
# /usr/lib/peacock/subparts-mount.sh (vendored from PRP's
# initramfs/rootfs/usr/lib/prp/subparts-mount.sh). This inline version stays as
# a fallback for now; once init scripts are switched to source the file (and
# the PRP-only branches that still rely on this code path are gone), this
# function should be removed.
setup_prp_like_subparts() {
    local userdata_dev="${1:-}"
    local boot_start=2048
    local root_start=1050624
    local boot_off=$((boot_start * 512))
    local root_off=$((root_start * 512))
    local probe_mnt="/run/peacock-root-probe"
    local gpt_sig=""
    local partx_cmd=""
    local partx_line=""
    local partx_root_start=""
    local fdisk_cmd=""
    local fdisk_line=""
    local fdisk_root_start=""
    local dmsetup_cmd=""
    local dm_root_name="peacock_root_init"
    local dm_root_dev=""
    local total_sectors=""
    local root_span_sectors=0
    local sect_path=""
    local cand=""
    local mount_src=""
    local main_loop=""
    local main_p2=""
    local root_loop=""
    local loop_magic=""
    local ptries=0
    local has_timeout=0
    local kernel_root_raw=""
    local dm_err=""

    [ -n "$userdata_dev" ] || userdata_dev="$(resolve_userdata_dev 2>/dev/null || true)"
    [ -b "$userdata_dev" ] || return 1
    has_busybox_applet timeout && has_timeout=1

    gpt_sig="$(/bin/busybox dd if="$userdata_dev" bs=1 skip=512 count=8 2>/dev/null || true)"
    [ "$gpt_sig" = "EFI PART" ] || {
        log "PRP-subparts: $userdata_dev is not GPT container (sig=${gpt_sig:-none})"
        return 1
    }
    log "PRP-subparts: probing $userdata_dev gpt_sig=$gpt_sig"

    # Prefer dynamic p2 start from nested GPT via partx first.
    for cand in /sbin/partx /usr/sbin/partx /usr/bin/partx /bin/partx; do
        [ -x "$cand" ] || continue
        partx_cmd="$cand"
        break
    done
    if [ -n "$partx_cmd" ]; then
        partx_line="$("$partx_cmd" -g -o START -nr 2 "$userdata_dev" 2>/dev/null | /bin/busybox head -n1 || true)"
        case "$partx_line" in
            ''|*[!0-9]*) : ;;
            *) partx_root_start="$partx_line" ;;
        esac
    fi

    # Fallback to fdisk when partx is unavailable or fails.
    for cand in /sbin/fdisk /usr/sbin/fdisk /usr/bin/fdisk /bin/fdisk; do
        [ -x "$cand" ] || continue
        fdisk_cmd="$cand"
        break
    done
    if [ -z "$fdisk_cmd" ] && /bin/busybox --list 2>/dev/null | /bin/busybox grep -qx fdisk; then
        fdisk_cmd="/bin/busybox fdisk"
    fi
    if [ -n "$fdisk_cmd" ]; then
        fdisk_line="$(
            /bin/busybox sh -c "$fdisk_cmd -l '$userdata_dev' 2>/dev/null" | \
            /bin/busybox awk -v d="$userdata_dev" '$1==d"p2"{print $2; exit}' || true
        )"
        case "$fdisk_line" in
            ''|*[!0-9]*) : ;;
            *) fdisk_root_start="$fdisk_line" ;;
        esac
    fi
    if [ -n "$partx_root_start" ]; then
        root_start="$partx_root_start"
    elif [ -n "$fdisk_root_start" ]; then
        root_start="$fdisk_root_start"
    fi
    root_off=$((root_start * 512))
    sect_path="/sys/class/block/${userdata_dev##*/}/size"
    if [ -r "$sect_path" ]; then
        total_sectors="$(/bin/busybox cat "$sect_path" 2>/dev/null || true)"
        case "$total_sectors" in
            ''|*[!0-9]*) total_sectors="" ;;
        esac
    fi
    if [ -n "$total_sectors" ] && [ "$total_sectors" -gt "$root_start" ]; then
        root_span_sectors=$((total_sectors - root_start))
    fi

    if has_busybox_applet partprobe; then
        /bin/busybox partprobe "$userdata_dev" >/dev/null 2>&1 || true
    fi
    if has_busybox_applet blockdev; then
        /bin/busybox blockdev --rereadpt "$userdata_dev" >/dev/null 2>&1 || true
    fi
    refresh_nested_candidates "$userdata_dev"

    # First try kernel-exposed nested nodes (p2/s1 aliases).
    log "PRP-subparts: checking kernel-exposed candidates"
    for cand in \
        "${userdata_dev}s1" "${userdata_dev}p2" \
        "/dev/${userdata_dev##*/}p2" "/dev/${userdata_dev##*/}s1" \
        "/dev/block/${userdata_dev##*/}p2" "/dev/block/${userdata_dev##*/}s1"; do
        [ -b "$cand" ] || continue
        [ -n "$kernel_root_raw" ] || kernel_root_raw="$cand"
        validate_root_candidate "$cand" || continue
        ROOT_DEV="$cand"
        log "PRP-subparts: using kernel-exposed root candidate $ROOT_DEV"
        ln -snf "$ROOT_DEV" "${userdata_dev}s1" 2>/dev/null || true
        return 0
    done
    if [ -n "$kernel_root_raw" ] && [ -b "$kernel_root_raw" ]; then
        ROOT_DEV="$kernel_root_raw"
        log "PRP-subparts: using kernel-exposed root candidate (unvalidated) $ROOT_DEV"
        ln -snf "$ROOT_DEV" "${userdata_dev}s1" 2>/dev/null || true
        return 0
    fi

    # Preferred fallback on this kernel: device-mapper linear root mapping.
    /bin/busybox mkdir -p /dev/mapper 2>/dev/null || true
    [ -c /dev/mapper/control ] || /bin/busybox mknod /dev/mapper/control c 10 236 2>/dev/null || true
    for cand in /sbin/dmsetup /usr/sbin/dmsetup /usr/bin/dmsetup /bin/dmsetup; do
        [ -x "$cand" ] || continue
        dmsetup_cmd="$cand"
        break
    done
    if [ -n "$dmsetup_cmd" ] && [ "$root_span_sectors" -gt 0 ]; then
        log "PRP-subparts: trying dm-linear root map start=$root_start sectors=$root_span_sectors"
        "$dmsetup_cmd" remove -f "$dm_root_name" >/dev/null 2>&1 || true
        if echo "0 $root_span_sectors linear $userdata_dev $root_start" | "$dmsetup_cmd" create "$dm_root_name" >/tmp/peacock-dm-root.err 2>&1; then
            "$dmsetup_cmd" mknodes >/dev/null 2>&1 || true
            dm_root_dev="/dev/mapper/$dm_root_name"
            if [ -b "$dm_root_dev" ]; then
                ROOT_DEV="$dm_root_dev"
                log "PRP-subparts: using dm-linear root candidate $ROOT_DEV"
                ln -snf "$ROOT_DEV" "${userdata_dev}s1" 2>/dev/null || true
                return 0
            fi
        else
            dm_err="$(/bin/busybox head -n 1 /tmp/peacock-dm-root.err 2>/dev/null || true)"
            log "PRP-subparts: dm-linear root create failed: $dm_err"
        fi
    fi

    # Preferred fallback: map nested GPT via a main loop and use loopXp2.
    if has_busybox_applet losetup; then
        log "PRP-subparts: trying loop partition mapping"
        main_loop="$(find_free_loop "" 2>/dev/null || true)"
        if [ -n "$main_loop" ] && [ -b "$main_loop" ]; then
            /bin/busybox losetup -d "$main_loop" >/dev/null 2>&1 || true
            if { [ "$has_timeout" -eq 1 ] && /bin/busybox timeout 3 /bin/busybox losetup -P "$main_loop" "$userdata_dev" >/dev/null 2>&1; } || \
               { [ "$has_timeout" -eq 0 ] && /bin/busybox losetup -P "$main_loop" "$userdata_dev" >/dev/null 2>&1; }; then
                if has_busybox_applet partprobe; then
                    /bin/busybox partprobe "$main_loop" >/dev/null 2>&1 || true
                fi
                if has_busybox_applet blockdev; then
                    /bin/busybox blockdev --rereadpt "$main_loop" >/dev/null 2>&1 || true
                fi
                while [ "$ptries" -lt 4 ]; do
                    /bin/busybox mdev -s >/dev/null 2>&1 || true
                    main_p2="$(ensure_block_node "${main_loop##*/}p2" 2>/dev/null || true)"
                    [ -n "$main_p2" ] && [ -b "$main_p2" ] && break
                    ptries=$((ptries + 1))
                    /bin/busybox sleep 1
                done
                if [ -n "$main_p2" ] && [ -b "$main_p2" ] && validate_root_candidate "$main_p2"; then
                    ROOT_DEV="$main_p2"
                    log "PRP-subparts: using loop-partition root candidate $ROOT_DEV"
                    ln -snf "$ROOT_DEV" "${userdata_dev}s1" 2>/dev/null || true
                    return 0
                fi
                if [ -n "$main_p2" ] && [ -b "$main_p2" ]; then
                    ROOT_DEV="$main_p2"
                    log "PRP-subparts: using loop-partition root candidate (unvalidated) $ROOT_DEV"
                    ln -snf "$ROOT_DEV" "${userdata_dev}s1" 2>/dev/null || true
                    return 0
                fi
            fi
        fi
    fi

    # Fallback: explicit offset loop on userdata container.
    if has_busybox_applet losetup; then
        log "PRP-subparts: trying explicit root offset loop off=$root_off"
        root_loop="$(find_free_loop "$main_loop" 2>/dev/null || true)"
        if [ -n "$root_loop" ] && [ -b "$root_loop" ]; then
            /bin/busybox losetup -d "$root_loop" >/dev/null 2>&1 || true
            if /bin/busybox losetup -o "$root_off" "$root_loop" "$userdata_dev" >/dev/null 2>&1; then
                loop_magic="$(
                    /bin/busybox dd if="$root_loop" bs=1 skip=1080 count=2 2>/dev/null | \
                    /bin/busybox hexdump -v -e '1/1 "%02x"' 2>/dev/null || true
                )"
                log "PRP-subparts: root loop offset probe off=$root_off magic=${loop_magic:-none}"
                if [ "$loop_magic" = "53ef" ]; then
                    ROOT_DEV="$root_loop"
                    log "PRP-subparts: using loop-offset root candidate $ROOT_DEV (magic-ok)"
                    ln -snf "$ROOT_DEV" "${userdata_dev}s1" 2>/dev/null || true
                    return 0
                fi
                /bin/busybox losetup -d "$root_loop" >/dev/null 2>&1 || true
            fi
        fi
    fi

    # Last fallback: implicit mount loop+offset, then reuse discovered mount source.
    /bin/busybox mkdir -p "$probe_mnt" 2>/dev/null || true
    if { [ "$has_timeout" -eq 1 ] && /bin/busybox timeout 3 /bin/busybox mount -t ext4 -o ro,loop,offset="$root_off" "$userdata_dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 0 ] && /bin/busybox mount -t ext4 -o ro,loop,offset="$root_off" "$userdata_dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 1 ] && /bin/busybox timeout 3 /bin/busybox mount -t ext4 -o ro,noload,loop,offset="$root_off" "$userdata_dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 0 ] && /bin/busybox mount -t ext4 -o ro,noload,loop,offset="$root_off" "$userdata_dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 1 ] && /bin/busybox timeout 3 /bin/busybox mount -t ext2 -o ro,loop,offset="$root_off" "$userdata_dev" "$probe_mnt" >/dev/null 2>&1; } || \
       { [ "$has_timeout" -eq 0 ] && /bin/busybox mount -t ext2 -o ro,loop,offset="$root_off" "$userdata_dev" "$probe_mnt" >/dev/null 2>&1; }; then
        mount_src="$(
            /bin/busybox awk -v m="$probe_mnt" '$2==m{print $1; exit}' /proc/mounts 2>/dev/null || true
        )"
        /bin/busybox umount "$probe_mnt" >/dev/null 2>&1 || true
        if [ -n "$mount_src" ] && [ -b "$mount_src" ]; then
            ROOT_DEV="$mount_src"
            log "PRP-subparts: using loop-offset root candidate $ROOT_DEV (off=$root_off)"
            ln -snf "$ROOT_DEV" "${userdata_dev}s1" 2>/dev/null || true
            return 0
        fi
    fi

    log "PRP-subparts: failed for $userdata_dev (p2_start=$root_start root_off=$root_off)"
    return 1
}

# If label lookup fails, dynamically discover userdata-like containers and ask kernel to expose subparts.
if [ -z "$ROOT_DEV" ] || [ ! -b "$ROOT_DEV" ]; then
    splash "Root detect: container scan..." 3
    add_container_candidates
    if [ -n "$CONTAINERS" ]; then
        log "findfs failed, probing GPT container devices:${CONTAINERS}"
    else
        log "findfs failed, no GPT container candidates discovered yet"
    fi
    for dev in $CONTAINERS; do
        if has_busybox_applet partprobe; then
            /bin/busybox partprobe "$dev" >/dev/null 2>&1 || true
        fi
        if has_busybox_applet blockdev; then
            /bin/busybox blockdev --rereadpt "$dev" >/dev/null 2>&1 || true
        fi
        add_probe_candidates_from_container "$dev"
        refresh_nested_candidates "$dev"
        # Keep container scan non-invasive. Deep loop probing is deferred to PRP-subparts.
    done
    /bin/busybox mdev -s >/dev/null 2>&1 || true
fi

# Retry label lookup after dynamic rescans.
if [ -z "$ROOT_DEV" ] || [ ! -b "$ROOT_DEV" ]; then
    splash "Root detect: label retry..." 3
    for i in 1 2 3 4 5 6; do
        find_root_by_label
        [ -n "$ROOT_DEV" ] && [ -b "$ROOT_DEV" ] && break
        /bin/busybox sleep 1
    done
fi



# If still unresolved, perform PRP-style userdata subpartition setup.
if [ -z "$ROOT_DEV" ] || [ ! -b "$ROOT_DEV" ]; then
    splash "Root detect: PRP subparts..." 3
    log "Entering PRP-subparts fallback"
    if setup_prp_like_subparts ""; then
        log "Using PRP-subparts root candidate: $ROOT_DEV"
    else
        log "PRP-subparts fallback did not find a root candidate"
    fi
fi

# Deep ext4 probe intentionally disabled to avoid device-specific I/O hangs.
# If no candidate was found above, we fail fast to shell below for interactive debugging.
if [ -z "$ROOT_DEV" ] || [ ! -b "$ROOT_DEV" ]; then
    splash "Root detect: no candidate" 3
    log "No root candidate selected; skipping deep ext4 probe to avoid hangs"
fi

# Flash 3x = root device search complete
flash_led 3 200

if [ -z "$ROOT_DEV" ] || [ ! -b "$ROOT_DEV" ]; then
    log "Error: Could not find root device with label {{.RootLabel}}"
    log "Available block devices:"
    /bin/busybox ls -la /dev/block/ 2>/dev/null | while read line; do log "BLOCK: $line"; done || /bin/busybox ls -la /dev/ | /bin/busybox grep -E "mmcblk|sd" | while read line; do log "DEV: $line"; done || true
    log "Dropping to shell..."
    exec /bin/busybox sh
fi

splash "Found root device: $ROOT_DEV" 3
bootlog_try_from_root "$ROOT_DEV" || true

# Resize root filesystem to fill partition (if device is larger than image)
# This is important when flashing to larger SD cards or eMMC
# resize2fs can resize unmounted filesystems, so we do it before mounting
splash "Resizing root filesystem..." 4
if [ -f /new_root/.peacock_resized ]; then
    log "Root filesystem already resized, skipping"
else
if [ -x /sbin/resize2fs ]; then
    /sbin/resize2fs "$ROOT_DEV" 2>&1 || echo "Warning: resize2fs failed (may already be correct size), continuing..."
elif command -v resize2fs >/dev/null 2>&1; then
    resize2fs "$ROOT_DEV" 2>&1 || echo "Warning: resize2fs failed (may already be correct size), continuing..."
elif /bin/busybox resize2fs "$ROOT_DEV" 2>/dev/null; then
    echo "Resized using busybox resize2fs"
else
    echo "Warning: resize2fs not available, filesystem may not fill partition"
fi
    touch /new_root/.peacock_resized 2>/dev/null || true
fi

splash "Mounting root filesystem..." 5
/bin/busybox mount -t ext4 "$ROOT_DEV" /new_root || \
    /bin/busybox mount -t ext4 -o noload "$ROOT_DEV" /new_root || \
    /bin/busybox mount -t ext2 "$ROOT_DEV" /new_root || \
    /bin/busybox mount "$ROOT_DEV" /new_root
mount_rc=$?

# Flash 4x = root filesystem mounted successfully
if [ $mount_rc -eq 0 ]; then
    flash_led 4 200
fi
if [ $mount_rc -ne 0 ]; then
    log "Error: Failed to mount $ROOT_DEV"
    splash "Error: Failed to mount $ROOT_DEV" 6
    # Try to copy log to rootfs before dropping to shell
    if [ -d /new_root ]; then
        cp /tmp/peacock-init.log /new_root/tmp/peacock-init.log 2>/dev/null || true
    fi
    exec /bin/busybox sh
fi

# Skip log copy before switch_root; some downstream kernels/filesystems can stall here.
# We only keep in-RAM /tmp/peacock-init.log during early boot.
log "Skipping pre-switch log copy"

# Handover to real init
log "Switching root to {{.InitSystem}}..."
splash "Switching root to {{.InitSystem}}..." 7
if [ -x /bin/peacock-splash ] && [ -n "$FBDEV" ]; then
	/bin/peacock-splash "Switching root to {{.InitSystem}}..." 7 "$FBDEV" 000000 noclear logo textmode "text=${PEACOCK_SPLASH_TEXT_COLOR:-FFFF00}" 2>&1 | while read line; do log "peacock-splash: $line"; done || true
fi
/bin/busybox mkdir -p /new_root/var/log 2>/dev/null || true
echo "attempt $(date +%s) init={{.InitSystem}}" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true

handoff_flare() {
    [ -x /bin/peacock-splash ] || { log "handoff flare: splash binary missing"; return 0; }
    if [ -z "$FBDEV" ]; then
        if [ -c /dev/fb0 ]; then
            FBDEV="/dev/fb0"
        elif [ -c /dev/graphics/fb0 ]; then
            FBDEV="/dev/graphics/fb0"
        elif [ -c /dev/fb1 ]; then
            FBDEV="/dev/fb1"
        elif [ -c /dev/graphics/fb1 ]; then
            FBDEV="/dev/graphics/fb1"
        fi
    fi
    [ -n "$FBDEV" ] || { log "handoff flare: FBDEV missing"; return 0; }
    local img=""
    for cand in /etc/peacock/conspiracy.png /conspiracy.png; do
        [ -f "$cand" ] || continue
        img="$cand"
        break
    done
    [ -n "$img" ] || { log "handoff flare: image missing"; return 0; }
    log "handoff flare: glitch+image ($img)"
    if /bin/busybox --list 2>/dev/null | /bin/busybox grep -qx timeout; then
        /bin/busybox timeout 1 /bin/peacock-splash " " 0 "$FBDEV" 000000 noclear glitch "image=$img" 2>&1 | while read line; do log "peacock-splash: $line"; done || true
    else
        /bin/peacock-splash " " 0 "$FBDEV" 000000 noclear glitch "image=$img" 2>&1 | while read line; do log "peacock-splash: $line"; done || true
    fi
    /bin/busybox usleep 60000 2>/dev/null || true
    # Avoid leaving the flare frame stuck if userspace display startup is delayed.
    /bin/peacock-splash " " 0 "$FBDEV" 000000 2>&1 | while read line; do log "peacock-splash: $line"; done || true
}

handoff_flare

# Flash 5x = about to switch_root
flash_led 5 200

stop_fb_refresher() {
    # switch_root can fail on some stacks if a long-running process keeps the initramfs root busy.
    # However, stopping the refresher too early can leave some panels black (warm reset),
    # so we only stop it as a fallback after a failed handoff.
    if [ -n "${REFRESHER_PID:-}" ]; then
        log "Stopping msm-fb-refresher (pid=$REFRESHER_PID)"
        kill "$REFRESHER_PID" 2>/dev/null || true
        /bin/busybox usleep 200000 2>/dev/null || true
        REFRESHER_PID=""
    fi
}
echo "preclose $(date +%s)" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
bootlog_close
echo "postclose $(date +%s)" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true

if [ "{{.InitSystem}}" = "systemd" ]; then
    /bin/busybox switch_root /new_root /usr/lib/systemd/systemd 2>/new_root/var/log/peacock-switch-root.err
    rc=$?
    stop_fb_refresher
    /bin/busybox switch_root /new_root /usr/lib/systemd/systemd 2>>/new_root/var/log/peacock-switch-root.err || rc=$?
    if [ -s /new_root/var/log/peacock-switch-root.err ]; then
        /bin/busybox cat /new_root/var/log/peacock-switch-root.err | while read line; do log "switch_root stderr: $line"; done
    fi
    log "switch_root to systemd failed with rc=$rc"
    echo "fail $(date +%s) systemd rc=$rc" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
    splash "switch_root failed (systemd)" 8
    flash_led 8 120
    exec /bin/busybox sh
elif [ "{{.InitSystem}}" = "openrc" ]; then
    # Ensure OpenRC has an inittab; some rootfs builds may miss it.
    if [ ! -f /new_root/etc/inittab ]; then
        log "Creating fallback /etc/inittab for OpenRC"
        /bin/busybox mkdir -p /new_root/etc 2>/dev/null || true
        cat > /new_root/etc/inittab <<'EOF'
::sysinit:/sbin/openrc sysinit
::wait:/sbin/openrc boot
::wait:/sbin/openrc default
::ctrlaltdel:/sbin/openrc reboot
::shutdown:/sbin/openrc shutdown
tty1::respawn:/sbin/agetty -L 115200 tty1 vt100
EOF
    fi
    # /dev is already provided by initramfs handoff. On kernels with
    # CONFIG_DEVTMPFS_MOUNT, remounting in OpenRC can emit noisy EBUSY and
    # occasionally destabilize early boot on some devices.
    /bin/busybox mkdir -p /new_root/etc/conf.d 2>/dev/null || true
    if ! /bin/busybox grep -q '^skip_mount_dev=' /new_root/etc/conf.d/devfs 2>/dev/null; then
        echo 'skip_mount_dev=yes' >> /new_root/etc/conf.d/devfs
    fi

    log "handoff via switch_root to openrc (/sbin/init)"
    log "handoff preflight: pid=$$ ppid=$PPID"
    if [ "$$" -ne 1 ]; then
        log "handoff preflight: warning pid is not 1; switch_root may be rejected"
    fi
    if /bin/busybox mountpoint -q /new_root 2>/dev/null; then
        log "handoff preflight: /new_root is a mountpoint"
    else
        log "handoff preflight: /new_root is NOT a mountpoint"
    fi
    /bin/busybox awk '$2=="/new_root"{print "handoff preflight: mount "$0}' /proc/mounts 2>/dev/null | while read line; do log "$line"; done
    preflight_mountpoint="no"
    if /bin/busybox mountpoint -q /new_root 2>/dev/null; then
        preflight_mountpoint="yes"
    fi
    preflight_mount_line="$(/bin/busybox awk '$2=="/new_root"{print $0; exit}' /proc/mounts 2>/dev/null || true)"
    preflight_init="no"
    [ -x /new_root/sbin/init ] && preflight_init="yes"
    preflight_console="no"
    [ -c /new_root/dev/console ] && preflight_console="yes"
    echo "preflight $(date +%s) pid=$$ ppid=$PPID mnt=$preflight_mountpoint init=$preflight_init console=$preflight_console line=${preflight_mount_line:-none}" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
    echo "handoff $(date +%s) switch_root-openrc-init" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
    /bin/busybox mkdir -p /new_root/dev 2>/dev/null || true
    # Ensure console node exists in new root even if devtmpfs move/reopen is flaky.
    [ -c /new_root/dev/console ] || /bin/busybox mknod -m 600 /new_root/dev/console c 5 1 2>/dev/null || true
    # Keep stderr on tmpfs to avoid failures opening a file on new root before handoff.
    : > /tmp/peacock-switch-root.err
    stop_fb_refresher
    # Keep fallback path alive: if switch_root fails, continue to chroot handoff.
    rc=0
    /bin/busybox switch_root -c /dev/console /new_root /sbin/init 2>>/tmp/peacock-switch-root.err || rc=$?
    if [ -s /tmp/peacock-switch-root.err ]; then
        /bin/busybox cat /tmp/peacock-switch-root.err | while read line; do log "switch_root stderr: $line"; done
        /bin/busybox cp /tmp/peacock-switch-root.err /new_root/var/log/peacock-switch-root.err 2>/dev/null || true
    fi
    log "switch_root to openrc failed with rc=$rc"
    echo "fail $(date +%s) openrc rc=$rc" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
    # Fallback for kernels/userspace combos where switch_root fails with no diagnostics:
    # move critical pseudo-fs and exec init from chroot. This keeps PID1 and usually
    # allows OpenRC to continue booting even when switch_root is rejected.
    log "trying fallback handoff via chroot (/sbin/init)"
    echo "fallback $(date +%s) chroot-openrc-init" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
    /bin/busybox mkdir -p /new_root/proc /new_root/sys /new_root/dev /new_root/run 2>/dev/null || true
    fallback_move_mount() {
        local src="$1"
        local dst="$2"
        if /bin/busybox mountpoint -q "$src" 2>/dev/null; then
            if /bin/busybox mount -o move "$src" "$dst" 2>>/tmp/peacock-switch-root.err; then
                log "handoff fallback: moved $src -> $dst"
            else
                log "handoff fallback: move failed $src -> $dst"
            fi
        else
            log "handoff fallback: source not a mountpoint: $src"
        fi
    }
    fallback_move_mount /proc /new_root/proc
    fallback_move_mount /sys /new_root/sys
    fallback_move_mount /dev /new_root/dev
    fallback_move_mount /run /new_root/run
    [ -c /new_root/dev/console ] || /bin/busybox mknod -m 600 /new_root/dev/console c 5 1 2>/dev/null || true
    if [ -s /tmp/peacock-switch-root.err ]; then
        /bin/busybox cat /tmp/peacock-switch-root.err | while read line; do log "handoff fallback stderr: $line"; done
        /bin/busybox cp /tmp/peacock-switch-root.err /new_root/var/log/peacock-switch-root.err 2>/dev/null || true
    fi
    echo "handoff $(date +%s) chroot-openrc-init" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
    cd /new_root || true
    exec /bin/busybox chroot /new_root /sbin/init
    rc=$?
    log "chroot handoff to openrc failed with rc=$rc"
    echo "fail $(date +%s) chroot-openrc rc=$rc" >> /new_root/var/log/peacock-switch-root.status 2>/dev/null || true
    splash "handoff failed (openrc)" 8
    flash_led 8 120
    exec /bin/busybox sh
else
    # Auto-detect fallback
    if [ -x /new_root/usr/lib/systemd/systemd ]; then
        /bin/busybox switch_root /new_root /usr/lib/systemd/systemd 2>/tmp/switch_root.err
        rc=$?
        if [ -s /tmp/switch_root.err ]; then
            /bin/busybox cat /tmp/switch_root.err | while read line; do log "switch_root stderr: $line"; done
        fi
        log "switch_root autodetect systemd failed with rc=$rc"
        splash "switch_root failed (autodetect systemd)" 8
        flash_led 8 120
        exec /bin/busybox sh
    elif [ -x /new_root/sbin/init ]; then
        /bin/busybox switch_root /new_root /sbin/init 2>/tmp/switch_root.err
        rc=$?
        if [ -s /tmp/switch_root.err ]; then
            /bin/busybox cat /tmp/switch_root.err | while read line; do log "switch_root stderr: $line"; done
        fi
        log "switch_root autodetect openrc failed with rc=$rc"
        splash "switch_root failed (autodetect openrc)" 8
        flash_led 8 120
        exec /bin/busybox sh
    else
        echo "No init found! Dropping to shell."
        exec /bin/busybox sh
    fi
fi
`

const initWrapperSource = `package main

import (
	"os"
	"syscall"
	"unsafe"
)

func klog(msg string) {
	if msg == "" {
		return
	}
	if f, err := os.OpenFile("/dev/kmsg", os.O_WRONLY, 0); err == nil {
		_, _ = f.Write([]byte(msg))
		_ = f.Close()
		return
	}
	b := []byte(msg)
	// SYSLOG_ACTION_WRITE = 2
	_, _, _ = syscall.Syscall(syscall.SYS_SYSLOG, 2, uintptr(unsafe.Pointer(&b[0])), uintptr(len(b)))
}

func main() {
	_ = os.MkdirAll("/dev", 0755)
	_ = syscall.Mount("devtmpfs", "/dev", "devtmpfs", 0, "")
	klog("PEACOCK: init wrapper start\n")
	env := os.Environ()
	tryExec := func(argv []string, label string) {
		klog("PEACOCK: exec " + label + "\n")
		_ = syscall.Exec(argv[0], argv, env)
	}

	// Prefer explicit busybox ash, then fall back to shell.
	tryExec([]string{"/bin/busybox", "ash", "/init.sh"}, "/bin/busybox ash /init.sh")
	tryExec([]string{"/bin/ash", "/init.sh"}, "/bin/ash /init.sh")
	tryExec([]string{"/bin/sh", "/init.sh"}, "/bin/sh /init.sh")
	tryExec([]string{"/bin/busybox", "sh"}, "/bin/busybox sh")

	klog("PEACOCK: init wrapper exec failed\n")
	os.Exit(1)
}
`

// GenerateInitScript writes the init script to the target path
func GenerateInitScript(path string, cfg InitConfig) error {
	tmpl, err := template.New("init").Parse(initScriptTemplate)
	if err != nil {
		return fmt.Errorf("failed to parse init template: %w", err)
	}

	var buf bytes.Buffer
	if err := tmpl.Execute(&buf, cfg); err != nil {
		return fmt.Errorf("failed to execute init template: %w", err)
	}

	if err := os.WriteFile(path, buf.Bytes(), 0755); err != nil {
		return fmt.Errorf("failed to write init script: %w", err)
	}

	return nil
}

func buildInitWrapper(outPath string, arch string) error {
	goarch := ""
	goarm := ""
	switch arch {
	case "armv7h":
		goarch = "arm"
		goarm = "7"
	case "armv7":
		goarch = "arm"
		goarm = "7"
	case "aarch64":
		goarch = "arm64"
	case "x86_64":
		goarch = "amd64"
	default:
		return fmt.Errorf("unsupported architecture for init wrapper: %s", arch)
	}

	tmpDir, err := os.MkdirTemp("", "peacock-init-wrapper-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir)

	srcPath := filepath.Join(tmpDir, "main.go")
	if err := os.WriteFile(srcPath, []byte(initWrapperSource), 0644); err != nil {
		return fmt.Errorf("failed to write init wrapper source: %w", err)
	}

	cmd := exec.Command("go", "build", "-trimpath", "-ldflags", "-s -w", "-o", outPath, srcPath)
	cmd.Stdout = runner.LogWriter()
	cmd.Stderr = runner.LogWriter()
	cmd.Env = append(os.Environ(), "CGO_ENABLED=0", "GOOS=linux", "GOARCH="+goarch)
	if goarm != "" {
		cmd.Env = append(cmd.Env, "GOARM="+goarm)
	}
	if err := runner.RunCmd(cmd); err != nil {
		return fmt.Errorf("failed to build init wrapper: %w", err)
	}

	return nil
}

func findFirstExisting(paths []string) string {
	for _, p := range paths {
		if p == "" {
			continue
		}
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return ""
}

func appendUniquePath(paths []string, p string) []string {
	if p == "" {
		return paths
	}
	clean := filepath.Clean(p)
	for _, existing := range paths {
		if existing == clean {
			return paths
		}
	}
	return append(paths, clean)
}

// runtimeVendorCandidates returns directories whose sbin/, bin/, usr/bin/,
// lib/, usr/lib/ trees are copied verbatim into the initramfs to provide rich
// util-linux tooling (losetup/partx/blkid/lsblk + shared libs) for nested-root
// probing. Historically this consumed `prp/vendor/<device>/rootfs-runtime`
// when PRP was vendored in-tree; that path is gone since the PRP split, so the
// canonical source is now the util-linux port build directory passed in via
// InitConfig.UtilLinuxBuildDir.
func runtimeVendorCandidates(utilLinuxBuildDir string) []string {
	var out []string
	if utilLinuxBuildDir != "" {
		out = appendUniquePath(out, utilLinuxBuildDir)
		// Some package layouts stage payloads under a "stage" subdir.
		out = appendUniquePath(out, filepath.Join(utilLinuxBuildDir, "stage"))
	}
	return out
}

// runtimeStageCandidates used to enumerate `prp/out/<device>/initramfs-stage`
// directories produced by a vendored PRP build. With PRP split out and no
// in-tree analogue yet, this returns nothing — keeping the function around
// so the rest of the dmsetup/library-search wiring can be extended cheaply
// when a future port emits an initramfs-stage tree.
func runtimeStageCandidates(deviceName string) []string {
	_ = deviceName
	return nil
}

func copyFileOrSymlink(src, dst string) error {
	info, err := os.Lstat(src)
	if err != nil {
		return err
	}

	if info.Mode()&os.ModeSymlink != 0 {
		target, err := os.Readlink(src)
		if err != nil {
			return err
		}
		_ = os.RemoveAll(dst)
		return os.Symlink(target, dst)
	}

	if !info.Mode().IsRegular() {
		return nil
	}

	content, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	mode := info.Mode() & 0o777
	if mode == 0 {
		mode = 0o644
	}
	return os.WriteFile(dst, content, mode)
}

func copyTree(srcRoot, dstRoot string) error {
	srcInfo, err := os.Stat(srcRoot)
	if err != nil {
		return err
	}
	if !srcInfo.IsDir() {
		return fmt.Errorf("source is not a directory: %s", srcRoot)
	}
	if err := os.MkdirAll(dstRoot, 0755); err != nil {
		return err
	}

	return filepath.Walk(srcRoot, func(path string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		rel, err := filepath.Rel(srcRoot, path)
		if err != nil {
			return err
		}
		if rel == "." {
			return nil
		}

		dst := filepath.Join(dstRoot, rel)
		if info.IsDir() {
			return os.MkdirAll(dst, info.Mode()&0o777)
		}
		if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
			return err
		}
		return copyFileOrSymlink(path, dst)
	})
}

// Build creates the initramfs cpio structure (Simulated for Prototype)
// In a real implementation:
// 1. Create temp dir
// 2. Copy busybox binary
// 3. GenerateInitScript
// 4. CPIO archive it 'find . | cpio -o -H newc > initramfs.cpio'
func Build(output string, cfg InitConfig) error {
	fmt.Printf("Generating init script for %s...\n", cfg.InitSystem)
	cfg.EnableS4CameraLED = enableS4CameraLED

	// 1. Create temp dir for initramfs structure
	tmpDir, err := os.MkdirTemp("", "peacock-initramfs-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir) // Clean up

	// 2. Create base directories
	for _, dir := range []string{"proc", "sys", "dev", "run", "tmp", "etc", "usr", "lib"} {
		if err := os.MkdirAll(filepath.Join(tmpDir, dir), 0755); err != nil {
			return fmt.Errorf("failed to create initramfs dir %s: %w", dir, err)
		}
	}

	// 3. Copy Busybox
	binDir := filepath.Join(tmpDir, "bin")
	if err := os.MkdirAll(binDir, 0755); err != nil {
		return err
	}

	// Copy busybox binary
	bbDest := filepath.Join(binDir, "busybox")
	input, err := os.ReadFile(cfg.BusyboxPath)
	if err != nil {
		return fmt.Errorf("failed to read busybox binary: %w", err)
	}
	if err := os.WriteFile(bbDest, input, 0755); err != nil {
		return fmt.Errorf("failed to write busybox binary: %w", err)
	}

	// Create symlinks for busybox applets (like /bin/sh, /bin/mount, etc.)
	// This is CRITICAL - without /bin/sh symlink, the kernel can't execute #!/bin/sh scripts!
	commonApplets := []string{
		"sh", "ash", "mount", "umount", "mknod", "mkdir", "rmdir",
		"cat", "ls", "cp", "mv", "rm", "ln", "chmod", "chown",
		"echo", "printf", "test", "[", "sleep", "usleep",
		"grep", "sed", "awk", "cut", "sort", "uniq", "head", "tail",
		"find", "xargs", "tar", "gzip", "gunzip", "cpio",
		"dd", "sync", "switch_root", "reboot", "poweroff", "halt",
		"true", "false", "date", "touch", "stat", "df", "du",
		"ps", "kill", "killall", "pidof", "top",
	}

	for _, applet := range commonApplets {
		symlinkPath := filepath.Join(binDir, applet)
		// Create relative symlink: bin/sh -> bin/busybox
		if err := os.Symlink("busybox", symlinkPath); err != nil {
			// Don't fail if symlink already exists or can't be created
			fmt.Printf("Warning: failed to create symlink %s: %v\n", applet, err)
		}
	}

	// Copy resize2fs binary (for rootfs resizing)
	sbinDir := filepath.Join(tmpDir, "sbin")
	if err := os.MkdirAll(sbinDir, 0755); err != nil {
		return err
	}

	resize2fsPath := cfg.Resize2fsPath
	if resize2fsPath == "" {
		// Try to find resize2fs in common locations
		for _, path := range []string{"/usr/sbin/resize2fs", "/sbin/resize2fs", "/usr/bin/resize2fs"} {
			if _, err := os.Stat(path); err == nil {
				resize2fsPath = path
				break
			}
		}
	}

	if resize2fsPath != "" {
		resize2fsDest := filepath.Join(sbinDir, "resize2fs")
		resize2fsInput, err := os.ReadFile(resize2fsPath)
		if err != nil {
			return fmt.Errorf("failed to read resize2fs binary: %w", err)
		}
		if err := os.WriteFile(resize2fsDest, resize2fsInput, 0755); err != nil {
			return fmt.Errorf("failed to write resize2fs binary: %w", err)
		}
	} else {
		fmt.Printf("Warning: resize2fs not found, rootfs resize will be skipped\n")
	}

	// Copy runtime userspace from the util-linux port build directory when
	// available. With 512MiB BOOT, we can carry richer util-linux tooling
	// (losetup/partx/blkid/lsblk + shared libs) for nested root probing.
	runtimeRoot := findFirstExisting(runtimeVendorCandidates(cfg.UtilLinuxBuildDir))
	stageRoots := runtimeStageCandidates(cfg.DeviceName)
	if runtimeRoot != "" {
		type runtimeCopy struct {
			srcRel string
			dstRel string
		}
		for _, item := range []runtimeCopy{
			{srcRel: "sbin", dstRel: "sbin"},
			{srcRel: "bin", dstRel: "bin"},
			{srcRel: filepath.Join("usr", "bin"), dstRel: filepath.Join("usr", "bin")},
			{srcRel: "lib", dstRel: "lib"},
			{srcRel: filepath.Join("usr", "lib"), dstRel: filepath.Join("usr", "lib")},
		} {
			srcDir := filepath.Join(runtimeRoot, item.srcRel)
			if _, err := os.Stat(srcDir); err != nil {
				continue
			}
			dstDir := filepath.Join(tmpDir, item.dstRel)
			if err := copyTree(srcDir, dstDir); err != nil {
				return fmt.Errorf("failed to copy runtime tree %s -> %s: %w", srcDir, dstDir, err)
			}
		}
	}

	// Locate dmsetup. Prefer the lvm2 port build directory (canonical source);
	// fall back to the util-linux runtime root (unlikely to have it, but keep
	// the legacy shape), then any stage roots, then host paths so dev builds
	// without the lvm2 port still produce a usable cpio.
	dmsetupCandidates := []string{}
	if cfg.Lvm2BuildDir != "" {
		dmsetupCandidates = append(dmsetupCandidates,
			filepath.Join(cfg.Lvm2BuildDir, "sbin", "dmsetup"),
			filepath.Join(cfg.Lvm2BuildDir, "stage", "sbin", "dmsetup"),
		)
	}
	if runtimeRoot != "" {
		dmsetupCandidates = append(dmsetupCandidates, filepath.Join(runtimeRoot, "sbin", "dmsetup"))
	}
	for _, stage := range stageRoots {
		dmsetupCandidates = append(dmsetupCandidates, filepath.Join(stage, "sbin", "dmsetup"))
	}
	dmsetupCandidates = append(dmsetupCandidates,
		"/sbin/dmsetup",
		"/usr/sbin/dmsetup",
		"/usr/bin/dmsetup",
		"/bin/dmsetup",
	)
	dmsetupPath := findFirstExisting(dmsetupCandidates)
	if dmsetupPath != "" {
		dmsetupDest := filepath.Join(sbinDir, "dmsetup")
		if _, err := os.Stat(dmsetupDest); err != nil {
			dmsetupInput, err := os.ReadFile(dmsetupPath)
			if err != nil {
				return fmt.Errorf("failed to read dmsetup binary: %w", err)
			}
			if err := os.WriteFile(dmsetupDest, dmsetupInput, 0755); err != nil {
				return fmt.Errorf("failed to write dmsetup binary: %w", err)
			}
		}

		libDir := filepath.Join(tmpDir, "lib")
		if err := os.MkdirAll(libDir, 0755); err != nil {
			return err
		}

		libSearchDirs := []string{}
		if cfg.Lvm2BuildDir != "" {
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "usr", "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "stage", "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(cfg.Lvm2BuildDir, "stage", "usr", "lib"))
		}
		if runtimeRoot != "" {
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(runtimeRoot, "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(runtimeRoot, "usr", "lib"))
		}
		for _, stage := range stageRoots {
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(stage, "lib"))
			libSearchDirs = appendUniquePath(libSearchDirs, filepath.Join(stage, "usr", "lib"))
		}
		libSearchDirs = appendUniquePath(libSearchDirs, "/lib")
		libSearchDirs = appendUniquePath(libSearchDirs, "/usr/lib")
		libSearchDirs = appendUniquePath(libSearchDirs, "/lib/arm-linux-gnueabihf")
		libSearchDirs = appendUniquePath(libSearchDirs, "/usr/lib/arm-linux-gnueabihf")
		requiredLibs := []string{
			"ld-linux-armhf.so.3",
			"libdevmapper.so.1.02",
			"libfdisk.so.1",
			"libfdisk.so.1.1.0",
			"libblkid.so.1",
			"libblkid.so.1.1.0",
			"libsmartcols.so.1",
			"libsmartcols.so.1.1.0",
			"libuuid.so.1",
			"libuuid.so.1.3.0",
			"libm.so.6",
			"libgcc_s.so.1",
			"libc.so.6",
		}
		for _, libName := range requiredLibs {
			var src string
			for _, d := range libSearchDirs {
				candidate := filepath.Join(d, libName)
				if _, err := os.Stat(candidate); err == nil {
					src = candidate
					break
				}
			}
			if src == "" {
				continue
			}
			dst := filepath.Join(libDir, libName)
			if _, err := os.Stat(dst); err == nil {
				continue
			}
			libInput, err := os.ReadFile(src)
			if err != nil {
				return fmt.Errorf("failed to read %s: %w", src, err)
			}
			if err := os.WriteFile(dst, libInput, 0755); err != nil {
				return fmt.Errorf("failed to write %s: %w", libName, err)
			}
		}
	} else {
		fmt.Printf("Warning: dmsetup not found, PRP-style dm-linear root probing will be unavailable\n")
	}

	// Copy peacock-splash binary (for framebuffer splash screen)
	if cfg.SplashPath != "" {
		splashDest := filepath.Join(binDir, "peacock-splash")
		splashInput, err := os.ReadFile(cfg.SplashPath)
		if err != nil {
			return fmt.Errorf("failed to read peacock-splash binary: %w", err)
		}
		if err := os.WriteFile(splashDest, splashInput, 0755); err != nil {
			return fmt.Errorf("failed to write peacock-splash binary: %w", err)
		}
	}

	// Optional handoff flare image used by initramfs right before root handover.
	conspiracySrc := findFirstExisting([]string{
		filepath.Join("conspiracy.png"),
		filepath.Join("assets", "conspiracy.png"),
		filepath.Join("prp", "assets", "conspiracy.png"),
	})
	if conspiracySrc != "" {
		conspiracyDir := filepath.Join(tmpDir, "etc", "peacock")
		if err := os.MkdirAll(conspiracyDir, 0755); err != nil {
			return fmt.Errorf("failed to create conspiracy image dir: %w", err)
		}
		conspiracyInput, err := os.ReadFile(conspiracySrc)
		if err != nil {
			return fmt.Errorf("failed to read conspiracy image: %w", err)
		}
		conspiracyDst := filepath.Join(conspiracyDir, "conspiracy.png")
		if err := os.WriteFile(conspiracyDst, conspiracyInput, 0644); err != nil {
			return fmt.Errorf("failed to write conspiracy image: %w", err)
		}
	}

	// Install the canonical Peacock sub-partition mount shell library into the
	// cpio at /usr/lib/peacock/subparts-mount.sh. The inline shell function
	// in this file is still emitted for now, but the long-term plan is to
	// replace it with `. /usr/lib/peacock/subparts-mount.sh` in the init script.
	subpartsSrc := findFirstExisting([]string{
		filepath.Join("assets", "initramfs", "subparts-mount.sh"),
		// Legacy compat: vendored PRP tree (kept for out-of-tree builds that
		// still hold a PRP checkout next to the Peacock source).
		filepath.Join("prp", "initramfs", "rootfs", "usr", "lib", "prp", "subparts-mount.sh"),
	})
	if subpartsSrc != "" {
		subpartsDir := filepath.Join(tmpDir, "usr", "lib", "peacock")
		if err := os.MkdirAll(subpartsDir, 0755); err != nil {
			return fmt.Errorf("failed to create subparts-mount dir: %w", err)
		}
		subpartsContent, err := os.ReadFile(subpartsSrc)
		if err != nil {
			return fmt.Errorf("failed to read subparts-mount.sh: %w", err)
		}
		subpartsDst := filepath.Join(subpartsDir, "subparts-mount.sh")
		if err := os.WriteFile(subpartsDst, subpartsContent, 0755); err != nil {
			return fmt.Errorf("failed to write subparts-mount.sh: %w", err)
		}
	} else {
		fmt.Printf("Warning: subparts-mount.sh not found in assets/initramfs/ or prp/initramfs/, using only inline subparts logic\n")
	}

	// Copy msm-fb-refresher binary (for MSM framebuffer refresh loop)
	if cfg.RefresherPath != "" {
		refresherDest := filepath.Join(binDir, "msm-fb-refresher")
		refresherInput, err := os.ReadFile(cfg.RefresherPath)
		if err != nil {
			return fmt.Errorf("failed to read msm-fb-refresher binary: %w", err)
		}
		if err := os.WriteFile(refresherDest, refresherInput, 0755); err != nil {
			return fmt.Errorf("failed to write msm-fb-refresher binary: %w", err)
		}
	}

	// 4. Generate Init Script
	initScriptPath := filepath.Join(tmpDir, "init.sh")
	if err := GenerateInitScript(initScriptPath, cfg); err != nil {
		return err
	}

	// 4b. Build binary /init wrapper so kernels without BINFMT_SCRIPT can boot
	initPath := filepath.Join(tmpDir, "init")
	if err := buildInitWrapper(initPath, cfg.Architecture); err != nil {
		return err
	}

	// 5. Create CPIO archive (newc format) and compress with gzip
	// Use find . | cpio -o -H newc | gzip to create initramfs.cpio.gz
	findCmd := exec.Command("find", ".")
	findCmd.Dir = tmpDir
	findCmd.Stderr = runner.LogWriter()

	cpioCmd := exec.Command("cpio", "-o", "-H", "newc")
	cpioCmd.Dir = tmpDir
	cpioCmd.Stderr = runner.LogWriter()

	gzipCmd := exec.Command("gzip", "-9")
	gzipCmd.Stderr = runner.LogWriter()

	// Pipe: find | cpio | gzip > output
	cpioCmd.Stdin, _ = findCmd.StdoutPipe()
	gzipCmd.Stdin, _ = cpioCmd.StdoutPipe()

	outFile, err := os.Create(output)
	if err != nil {
		return fmt.Errorf("failed to create output file: %w", err)
	}
	defer outFile.Close()
	gzipCmd.Stdout = outFile

	// Start commands in reverse order (gzip -> cpio -> find)
	if err := gzipCmd.Start(); err != nil {
		return fmt.Errorf("failed to start gzip: %w", err)
	}
	if err := cpioCmd.Start(); err != nil {
		gzipCmd.Wait()
		return fmt.Errorf("failed to start cpio: %w", err)
	}
	if err := findCmd.Start(); err != nil {
		cpioCmd.Wait()
		gzipCmd.Wait()
		return fmt.Errorf("failed to start find: %w", err)
	}

	// Wait for all commands to complete
	if err := findCmd.Wait(); err != nil {
		cpioCmd.Wait()
		gzipCmd.Wait()
		return fmt.Errorf("find failed: %w", err)
	}
	if err := cpioCmd.Wait(); err != nil {
		gzipCmd.Wait()
		return fmt.Errorf("cpio failed: %w", err)
	}
	if err := gzipCmd.Wait(); err != nil {
		return fmt.Errorf("gzip failed: %w", err)
	}

	return nil
}
