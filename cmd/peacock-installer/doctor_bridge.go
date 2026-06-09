package main

// RunDoctor binding — installer-side variant of the builder's doctor
// probe. The builder ships the full host probe table (qemu-static,
// cross-gcc, pacman/apt/apk, etc.) because it has to assemble a rootfs
// for an arbitrary target. The installer only writes a pre-built
// rootfs to disk, so its prerequisite set is much smaller:
//
//   - lsblk         — disk enumeration (already used by ListDisks)
//   - parted        — partition table writes
//   - mkfs.ext4     — root filesystem
//   - mkfs.vfat     — ESP on UEFI installs
//   - rsync         — live → target copy
//   - grub-install  — bootloader on UEFI
//   - extlinux      — bootloader on BIOS / ARM
//   - chroot, mount, umount — present in every coreutils install but
//     worth confirming so the doctor screen never surfaces a green
//     check just to fail mid-install.
//
// We could reuse internal/host's table-driven probe runner, but its
// canonical table is loaded with builder-specific entries we don't
// want. Easier to build a small installer-local probe list here.

import (
	"os/exec"
)

// DoctorReport is the JSON-shaped result the React side consumes.
// Same shape as the builder's DoctorReport so the existing "Check
// host" tile renders without any frontend changes.
type DoctorReport struct {
	Summary DoctorSummary    `json:"summary"`
	Results []ProbeResultDTO `json:"results"`
}

// DoctorSummary mirrors the builder's DoctorSummary verbatim.
type DoctorSummary struct {
	OK      int `json:"ok"`
	Missing int `json:"missing"`
	Broken  int `json:"broken"`
	Skipped int `json:"skipped"`
}

// ProbeResultDTO matches the builder's per-probe JSON shape so the
// React mock's section-header / icon / install-hint rendering is
// identical between the two binaries.
type ProbeResultDTO struct {
	Group       string `json:"group"`
	Name        string `json:"name"`
	Path        string `json:"path,omitempty"`
	Version     string `json:"version,omitempty"`
	Status      string `json:"status"`
	InstallHint string `json:"install_hint,omitempty"`
	Why         string `json:"why,omitempty"`
}

// installerProbe is the local probe entry used to build the doctor
// report. Tiny on purpose — we only need binary + group + hint.
type installerProbe struct {
	Group string
	Name  string
	Bins  []string // first match on PATH wins
	Hint  string
	Why   string
}

// installerProbes lists what RunInstall actually shells out to. Order
// matches the install-pipeline phase order: storage tools first,
// filesystem tools, bootloader, then the chroot-helper coreutils
// every pipeline assumes.
var installerProbes = []installerProbe{
	{Group: "storage", Name: "lsblk", Bins: []string{"lsblk"},
		Hint: "install util-linux", Why: "lists candidate target disks"},
	{Group: "storage", Name: "parted", Bins: []string{"parted"},
		Hint: "install parted", Why: "writes partition table on target"},
	{Group: "storage", Name: "wipefs", Bins: []string{"wipefs"},
		Hint: "install util-linux", Why: "clears old signatures on target"},
	{Group: "storage", Name: "partprobe", Bins: []string{"partprobe"},
		Hint: "install parted", Why: "asks kernel to re-read partition table"},
	{Group: "filesystem", Name: "mkfs.ext4", Bins: []string{"mkfs.ext4"},
		Hint: "install e2fsprogs", Why: "formats the root filesystem"},
	{Group: "filesystem", Name: "mkfs.vfat", Bins: []string{"mkfs.vfat", "mkfs.fat"},
		Hint: "install dosfstools", Why: "formats the EFI system partition"},
	{Group: "filesystem", Name: "mkfs.ext2", Bins: []string{"mkfs.ext2"},
		Hint: "install e2fsprogs", Why: "formats /boot on extlinux/BIOS installs"},
	{Group: "filesystem", Name: "rsync", Bins: []string{"rsync"},
		Hint: "install rsync", Why: "copies the live system to the target"},
	{Group: "bootloader", Name: "grub-install", Bins: []string{"grub-install"},
		Hint: "install grub", Why: "installs GRUB on UEFI / BIOS installs"},
	{Group: "bootloader", Name: "extlinux", Bins: []string{"extlinux"},
		Hint: "install syslinux / extlinux", Why: "installs extlinux on ARM / non-EFI installs"},
	{Group: "chroot", Name: "chroot", Bins: []string{"chroot"},
		Hint: "install coreutils", Why: "runs target-system commands post-copy"},
	{Group: "chroot", Name: "mount", Bins: []string{"mount"},
		Hint: "install util-linux", Why: "mounts target partitions during install"},
	{Group: "chroot", Name: "umount", Bins: []string{"umount"},
		Hint: "install util-linux", Why: "unmounts target on cleanup"},
}

// RunDoctor walks installerProbes, doing a PATH lookup per probe, and
// returns the JSON-shaped report. We don't take any args (unlike the
// builder's flavor/device/useHostChroot triple) because the installer
// doesn't have flavor / cross-arch dimensions to filter on.
func (a *App) RunDoctor() (DoctorReport, error) {
	results := make([]ProbeResultDTO, 0, len(installerProbes))
	var summary DoctorSummary
	for _, p := range installerProbes {
		r := ProbeResultDTO{
			Group:       p.Group,
			Name:        p.Name,
			InstallHint: p.Hint,
			Why:         p.Why,
			Status:      "missing",
		}
		for _, bin := range p.Bins {
			if path, err := exec.LookPath(bin); err == nil {
				r.Path = path
				r.Status = "ok"
				break
			}
		}
		switch r.Status {
		case "ok":
			summary.OK++
		case "missing":
			summary.Missing++
		case "broken":
			summary.Broken++
		case "skipped":
			summary.Skipped++
		}
		results = append(results, r)
	}
	return DoctorReport{Summary: summary, Results: results}, nil
}
