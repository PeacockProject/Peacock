package installer

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// DiskInfo is one entry in the disk picker shown by the installer GUI.
// Cap is a human-readable size (lsblk's default format) while SizeBytes is
// the raw byte count for sorting / sanity-checks.
type DiskInfo struct {
	Node      string
	Name      string
	Cap       string
	SizeBytes uint64
	Removable bool
	Children  []PartInfo
}

// PartInfo describes an existing partition under a disk. The GUI shows
// these so the user can spot they're about to nuke their data partition.
type PartInfo struct {
	Node       string
	Size       string
	FSType     string
	Mountpoint string
}

// rawLsblkDevice matches the fields requested below in -o. lsblk omits
// nil fields entirely in some versions; pointers let us notice missing
// vs. zero.
type rawLsblkDevice struct {
	Name       string           `json:"name"`
	Type       string           `json:"type"`
	Size       *json.Number     `json:"size"`
	Model      *string          `json:"model"`
	Mountpoint *string          `json:"mountpoint"`
	RM         interface{}      `json:"rm"`
	Vendor     *string          `json:"vendor"`
	Fstype     *string          `json:"fstype"`
	Children   []rawLsblkDevice `json:"children"`
}

// ListDisks returns all top-level disks suitable for installation. The
// live medium itself is filtered out — bricking the live USB midway is
// the worst-case foot-gun.
func ListDisks(ctx context.Context) ([]DiskInfo, error) {
	// -b: bytes; -J: JSON; -o: explicit column list to keep the schema stable
	// across lsblk versions. NAME,TYPE,SIZE,MODEL,MOUNTPOINT,RM,VENDOR,FSTYPE.
	cmd := exec.CommandContext(ctx, "lsblk", "-J", "-b",
		"-o", "NAME,TYPE,SIZE,MODEL,MOUNTPOINT,RM,VENDOR,FSTYPE")
	out, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("installer: lsblk: %w", err)
	}
	return parseLsblk(out, liveMediumDisk())
}

// parseLsblk is split out so tests can feed in a fixture JSON without
// needing lsblk on the test host. excludeDisk, when non-empty, suppresses
// that top-level disk (used to hide the live USB).
func parseLsblk(data []byte, excludeDisk string) ([]DiskInfo, error) {
	var doc struct {
		BlockDevices []rawLsblkDevice `json:"blockdevices"`
	}
	if err := json.Unmarshal(data, &doc); err != nil {
		return nil, fmt.Errorf("installer: parse lsblk JSON: %w", err)
	}

	out := make([]DiskInfo, 0, len(doc.BlockDevices))
	for _, d := range doc.BlockDevices {
		if d.Type != "disk" {
			continue
		}
		// Skip zram, ram, loop devices — never an install target.
		if strings.HasPrefix(d.Name, "zram") || strings.HasPrefix(d.Name, "ram") ||
			strings.HasPrefix(d.Name, "loop") {
			continue
		}
		node := "/dev/" + d.Name
		if excludeDisk != "" && node == excludeDisk {
			continue
		}
		bytes, _ := numberToUint64(d.Size)
		info := DiskInfo{
			Node:      node,
			Name:      diskDisplayName(d),
			SizeBytes: bytes,
			Cap:       humanSize(bytes),
			Removable: rmToBool(d.RM),
		}
		for _, c := range d.Children {
			if c.Type != "part" {
				continue
			}
			cbytes, _ := numberToUint64(c.Size)
			info.Children = append(info.Children, PartInfo{
				Node:       "/dev/" + c.Name,
				Size:       humanSize(cbytes),
				FSType:     deref(c.Fstype),
				Mountpoint: deref(c.Mountpoint),
			})
		}
		out = append(out, info)
	}
	return out, nil
}

func diskDisplayName(d rawLsblkDevice) string {
	vendor := strings.TrimSpace(deref(d.Vendor))
	model := strings.TrimSpace(deref(d.Model))
	switch {
	case vendor != "" && model != "":
		return vendor + " " + model
	case model != "":
		return model
	case vendor != "":
		return vendor
	case strings.HasPrefix(d.Name, "nvme"):
		return "Internal · NVMe"
	case strings.HasPrefix(d.Name, "mmcblk"):
		return "Internal · eMMC"
	case strings.HasPrefix(d.Name, "sd"):
		return "SATA / USB Disk"
	default:
		return d.Name
	}
}

// rmToBool tolerates lsblk variants that emit "1"/"0" strings vs.
// booleans vs. numbers.
func rmToBool(v interface{}) bool {
	switch x := v.(type) {
	case bool:
		return x
	case float64:
		return x != 0
	case string:
		return x == "1" || x == "true"
	case json.Number:
		i, _ := x.Int64()
		return i != 0
	default:
		return false
	}
}

func numberToUint64(n *json.Number) (uint64, error) {
	if n == nil {
		return 0, nil
	}
	i, err := n.Int64()
	if err != nil {
		return 0, err
	}
	if i < 0 {
		return 0, nil
	}
	return uint64(i), nil
}

func deref(p *string) string {
	if p == nil {
		return ""
	}
	return *p
}

// humanSize is a small human-readable formatter that doesn't drag in a
// dep. Matches lsblk's default-ish output (e.g. "16G", "500G", "1.8T").
func humanSize(b uint64) string {
	const k = 1024
	if b < k {
		return fmt.Sprintf("%d B", b)
	}
	units := []string{"K", "M", "G", "T", "P"}
	val := float64(b)
	i := -1
	for val >= k && i < len(units)-1 {
		val /= k
		i++
	}
	if val >= 100 || val == float64(int64(val)) {
		return fmt.Sprintf("%d %sB", int64(val), units[i])
	}
	return fmt.Sprintf("%.1f %sB", val, units[i])
}

// liveMediumDisk returns the canonical disk node ("/dev/sda" given
// "/dev/sda1") hosting the live rootfs, or "" when we can't tell. The
// detection looks for boot=live in /proc/cmdline; if found, it resolves
// /run/live's backing device via /proc/mounts and strips the partition
// suffix.
//
// Defensive — if anything is ambiguous we return "" rather than risk
// excluding a real install target.
func liveMediumDisk() string {
	cmdline, err := os.ReadFile("/proc/cmdline")
	if err != nil {
		return ""
	}
	if !strings.Contains(string(cmdline), "boot=live") {
		return ""
	}
	mounts, err := os.ReadFile("/proc/mounts")
	if err != nil {
		return ""
	}
	for _, line := range strings.Split(string(mounts), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		if fields[1] == "/run/live" || strings.HasPrefix(fields[1], "/run/live/") {
			return partitionToDisk(fields[0])
		}
	}
	return ""
}

// partitionToDisk maps "/dev/sda1" → "/dev/sda" and "/dev/mmcblk0p1" →
// "/dev/mmcblk0". Returns "" for non-/dev paths.
func partitionToDisk(p string) string {
	if !strings.HasPrefix(p, "/dev/") {
		return ""
	}
	name := filepath.Base(p)
	// nvme + mmc carry a 'p' between the disk and the partition number.
	if strings.HasPrefix(name, "nvme") || strings.HasPrefix(name, "mmcblk") {
		if idx := strings.LastIndex(name, "p"); idx > 0 {
			return "/dev/" + name[:idx]
		}
		return p
	}
	// strip trailing digits
	cut := len(name)
	for cut > 0 && name[cut-1] >= '0' && name[cut-1] <= '9' {
		cut--
	}
	if cut == len(name) {
		return p
	}
	return "/dev/" + name[:cut]
}
