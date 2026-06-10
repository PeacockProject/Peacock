// Package host provides read-only host-environment probes used by
// `peacock doctor` and (later) by the build path to gate early failures
// with actionable error messages.
//
// The probe set is table-driven: each probe knows what host tool / file
// it looks for, which install hint to surface, and which build modes
// (flavor, device-family, feather/etc.) it applies to. Callers filter
// the table via ProbeOpts and run Probe over each entry.
//
// This package must not import anything that talks to the network or
// shells out beyond `exec.LookPath` and a handful of cheap version
// queries. It is also imported by tests and by cmd/peacock/doctor.go
// so the dependency surface must stay tiny.
package host

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"

	"peacock/internal/ports"
)

// Status is the outcome of a single probe.
type Status string

const (
	// StatusOK means the tool was found and (when applicable) parsed a
	// sensible version.
	StatusOK Status = "ok"
	// StatusMissing means the tool wasn't found on PATH / disk.
	StatusMissing Status = "missing"
	// StatusBroken means the tool was found but failed a version check
	// (e.g. too old) or refused to report a version.
	StatusBroken Status = "broken"
	// StatusSkipped means the probe was filtered out by ProbeOpts and
	// didn't run. Callers should hide these from summaries.
	StatusSkipped Status = "skipped"
)

// Group buckets a probe into a section of `peacock doctor` output.
type Group string

const (
	GroupCoreBuild  Group = "core build"
	GroupCrossArch  Group = "cross-arch"
	GroupBootloader Group = "bootloader"
	GroupDevice     Group = "device"
	GroupFeather    Group = "feather"
	GroupFilesystem Group = "filesystem"
	GroupHostChroot Group = "host-chroot"
)

// Result is what Probe returns for one entry in the table.
type Result struct {
	Group       Group  `json:"group"`
	Name        string `json:"name"`
	Path        string `json:"path,omitempty"`
	Version     string `json:"version,omitempty"`
	Status      Status `json:"status"`
	InstallHint string `json:"install_hint,omitempty"`
	Why         string `json:"why,omitempty"`
}

// ProbeOpts filters the table-driven probe set down to what's relevant
// for the build the user is about to run. All fields are optional; a
// zero ProbeOpts runs every probe across every flavor and device-family
// (the union — what "everything every flavor + every supported device
// family needs" means).
type ProbeOpts struct {
	// Flavor restricts cross-arch / bootstrap probes to a single
	// flavor ("arch", "debian", "alpine"). Empty means union (run all).
	Flavor string
	// DeviceFamily restricts device-side probes to a single family
	// (e.g. "android", "fastboot"). Empty means union.
	DeviceFamily string
	// UseHostChroot, when non-empty, collapses the probe set to the
	// minimal chroot bootstrap set (chroot + tar + curl) plus
	// host-chroot-specific probes. The flavor-specific bootstrap tools
	// are still listed but get StatusSkipped: they live inside the
	// host chroot, not on the host, and doctor will say so.
	UseHostChroot string
}

// Probe describes one entry in the host-tool audit table.
type Probe struct {
	Group       Group
	Name        string
	Why         string
	InstallHint string

	// Flavors lists the flavors this probe applies to. Empty = all
	// flavors.
	Flavors []string
	// DeviceFamilies lists the device families this probe applies to.
	// Empty = all device families. Probes that don't care about device
	// (e.g. core build tools) leave this empty.
	DeviceFamilies []string
	// HostOnly, when true, means this probe must always run on the
	// host even in --use-host-chroot mode (e.g. `chroot`, `tar`, the
	// host-chroot root path).
	HostOnly bool
	// CollapsedByHostChroot, when true, means this probe is rendered
	// as StatusSkipped when --use-host-chroot is in use, because the
	// tool would be looked up inside the chroot instead.
	CollapsedByHostChroot bool

	// run actually performs the probe and fills in Path/Version/Status.
	run func(p *Probe) (string, string, Status)
}

// matches reports whether this probe should be visible given opts.
func (p *Probe) matches(opts ProbeOpts) bool {
	if opts.Flavor != "" && len(p.Flavors) > 0 {
		hit := false
		for _, f := range p.Flavors {
			if f == opts.Flavor {
				hit = true
				break
			}
		}
		if !hit {
			return false
		}
	}
	if opts.DeviceFamily != "" && len(p.DeviceFamilies) > 0 {
		hit := false
		for _, f := range p.DeviceFamilies {
			if f == opts.DeviceFamily {
				hit = true
				break
			}
		}
		if !hit {
			return false
		}
	}
	if opts.DeviceFamily == "" && len(p.DeviceFamilies) > 0 {
		// Device probes only show up when a device is requested.
		return false
	}
	return true
}

// Run executes one probe and returns a Result, honoring host-chroot
// collapse rules.
func (p *Probe) Run(opts ProbeOpts) Result {
	r := Result{
		Group:       p.Group,
		Name:        p.Name,
		InstallHint: p.InstallHint,
		Why:         p.Why,
	}
	if opts.UseHostChroot != "" && p.CollapsedByHostChroot && !p.HostOnly {
		r.Status = StatusSkipped
		if r.Why == "" {
			r.Why = "looked up inside --use-host-chroot=" + opts.UseHostChroot
		} else {
			r.Why = r.Why + " (looked up inside --use-host-chroot=" + opts.UseHostChroot + ")"
		}
		return r
	}
	if p.run == nil {
		r.Status = StatusBroken
		r.Why = "probe has no runner"
		return r
	}
	path, version, status := p.run(p)
	r.Path = path
	r.Version = version
	r.Status = status
	return r
}

// Table returns the canonical, ordered list of probes. The order is
// stable so doctor output is deterministic and tests can assert on it.
func Table() []Probe {
	return tableCopy(probeTable)
}

func tableCopy(in []Probe) []Probe {
	out := make([]Probe, len(in))
	copy(out, in)
	return out
}

// FilterAndRun applies ProbeOpts to Table() and runs each surviving
// probe. Useful for tests and for the doctor command.
func FilterAndRun(opts ProbeOpts) []Result {
	results := make([]Result, 0)
	for _, p := range Table() {
		if !p.matches(opts) {
			continue
		}
		results = append(results, p.Run(opts))
	}
	return results
}

// --- runners -------------------------------------------------------------

// lookPath wraps exec.LookPath into the probe-runner signature.
func lookPath(_ *Probe) (string, string, Status) {
	// Replaced per-probe via lookPathFor.
	return "", "", StatusBroken
}

// lookPathFor returns a runner that searches PATH for `name`.
func lookPathFor(name string) func(*Probe) (string, string, Status) {
	return func(p *Probe) (string, string, Status) {
		path, err := exec.LookPath(name)
		if err != nil {
			return "", "", StatusMissing
		}
		return path, "", StatusOK
	}
}

// lookPathAny returns a runner that searches PATH for any of `names`.
// First match wins. Used for tools that ship under multiple
// distro-specific binary names (e.g. apk vs apk.static).
func lookPathAny(names ...string) func(*Probe) (string, string, Status) {
	return func(p *Probe) (string, string, Status) {
		for _, n := range names {
			if path, err := exec.LookPath(n); err == nil {
				return path, "", StatusOK
			}
		}
		return "", "", StatusMissing
	}
}

// lookPathWithVersion looks `name` up on PATH, runs `<name> --version`,
// extracts a semver-shaped string, and compares against minVer. If
// minVer is empty the version is recorded but not enforced.
func lookPathWithVersion(name, versionArg, minVer string) func(*Probe) (string, string, Status) {
	return func(p *Probe) (string, string, Status) {
		path, err := exec.LookPath(name)
		if err != nil {
			return "", "", StatusMissing
		}
		ver := runVersion(path, versionArg)
		if ver == "" {
			if minVer == "" {
				return path, "", StatusOK
			}
			return path, "", StatusBroken
		}
		if minVer != "" && !versionAtLeast(ver, minVer) {
			return path, ver, StatusBroken
		}
		return path, ver, StatusOK
	}
}

var (
	semverRE = regexp.MustCompile(`\d+\.\d+(?:\.\d+)?`)
)

func runVersion(bin, arg string) string {
	if arg == "" {
		arg = "--version"
	}
	out, err := exec.Command(bin, arg).CombinedOutput()
	if err != nil {
		// Some tools (e.g. python3) write to stderr or use -V. Try -V
		// once before giving up.
		if arg != "-V" {
			out, err = exec.Command(bin, "-V").CombinedOutput()
		}
		if err != nil {
			return ""
		}
	}
	m := semverRE.FindString(string(out))
	return m
}

// versionAtLeast does a numeric-component compare on dotted versions.
// Trailing missing components default to 0.
func versionAtLeast(have, want string) bool {
	h := splitVerParts(have)
	w := splitVerParts(want)
	n := len(w)
	if len(h) > n {
		n = len(h)
	}
	for i := 0; i < n; i++ {
		var hv, wv int
		if i < len(h) {
			hv = h[i]
		}
		if i < len(w) {
			wv = w[i]
		}
		if hv > wv {
			return true
		}
		if hv < wv {
			return false
		}
	}
	return true
}

func splitVerParts(v string) []int {
	parts := strings.Split(v, ".")
	out := make([]int, 0, len(parts))
	for _, p := range parts {
		n := 0
		for _, c := range p {
			if c < '0' || c > '9' {
				break
			}
			n = n*10 + int(c-'0')
		}
		out = append(out, n)
	}
	return out
}

// fileExistsRunner returns a runner that just stats `path` (no
// version, no PATH lookup). Used for /proc-style checks.
func fileExistsRunner(path string) func(*Probe) (string, string, Status) {
	return func(p *Probe) (string, string, Status) {
		if _, err := os.Stat(path); err != nil {
			if os.IsNotExist(err) {
				return path, "", StatusMissing
			}
			return path, "", StatusBroken
		}
		return path, "", StatusOK
	}
}

// sudoNoPrompt checks `sudo -n true` exit status. StatusOK means
// passwordless sudo is configured; StatusBroken means sudo is on PATH
// but a password is required (the build will hang); StatusMissing
// means no sudo at all.
func sudoNoPrompt(_ *Probe) (string, string, Status) {
	path, err := exec.LookPath("sudo")
	if err != nil {
		return "", "", StatusMissing
	}
	cmd := exec.Command(path, "-n", "true")
	if err := cmd.Run(); err != nil {
		return path, "", StatusBroken
	}
	return path, "", StatusOK
}

// ftrRunner mirrors internal/feather.resolveBinary without importing
// feather (which would pull runner/log infra into a read-only audit).
func ftrRunner(_ *Probe) (string, string, Status) {
	if p, err := exec.LookPath("ftr"); err == nil {
		ver := runVersion(p, "--version")
		return p, ver, StatusOK
	}
	const fallback = "/peacock/bin/ftr"
	if _, err := os.Stat(fallback); err == nil {
		ver := runVersion(fallback, "--version")
		return fallback, ver, StatusOK
	}
	return "", "", StatusMissing
}

// hostChrootRootRunner reports whether the user has already bootstrapped
// a host chroot for a flavor. Path returned is the would-be root dir.
func hostChrootRootRunner(flavor string) func(*Probe) (string, string, Status) {
	return func(p *Probe) (string, string, Status) {
		root, err := HostChrootRoot(flavor)
		if err != nil {
			return "", "", StatusBroken
		}
		if _, err := os.Stat(root); err == nil {
			return root, "", StatusOK
		}
		return root, "", StatusMissing
	}
}

// portsPresentRunner reports whether a peacock-ports checkout already
// resolves on this host. Read-only: it stats candidate dirs via
// ports.Resolve and never clones (doctor must not fetch). Path returned
// is the resolved tree when found.
func portsPresentRunner(_ *Probe) (string, string, Status) {
	if root, ok := ports.Resolve(); ok {
		return root, "", StatusOK
	}
	return "", "", StatusMissing
}

// --- the table -----------------------------------------------------------

var probeTable = []Probe{
	// core build
	{
		Group:       GroupCoreBuild,
		Name:        "go",
		Why:         "Peacock CLI is written in Go (>=1.25.5)",
		InstallHint: "arch: pacman -S go | debian: apt install golang-go | alpine: apk add go",
		run:         lookPathWithVersion("go", "version", "1.25.5"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "clang",
		Why:         "C/C++ toolchain for kernel and bootloader builds",
		InstallHint: "arch: pacman -S clang | debian: apt install clang | alpine: apk add clang",
		run:         lookPathWithVersion("clang", "--version", ""),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "lld",
		Why:         "LLVM linker — used by kernel LLD=1 builds",
		InstallHint: "arch: pacman -S lld | debian: apt install lld | alpine: apk add lld",
		run:         lookPathFor("ld.lld"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "gcc",
		Why:         "C/C++ toolchain (alternative to clang)",
		InstallHint: "arch: pacman -S gcc | debian: apt install gcc | alpine: apk add gcc",
		run:         lookPathWithVersion("gcc", "--version", ""),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "make",
		Why:         "kernel + most ports build via make",
		InstallHint: "arch: pacman -S make | debian: apt install make | alpine: apk add make",
		run:         lookPathFor("make"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "cmake",
		Why:         "several ports build via cmake (peacock-splash, qt deps, ...)",
		InstallHint: "arch: pacman -S cmake | debian: apt install cmake | alpine: apk add cmake",
		run:         lookPathWithVersion("cmake", "--version", ""),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "ninja",
		Why:         "fast build backend used by cmake-driven ports",
		InstallHint: "arch: pacman -S ninja | debian: apt install ninja-build | alpine: apk add samurai",
		run:         lookPathFor("ninja"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "git",
		Why:         "fetches kernel sources + submodules",
		InstallHint: "arch: pacman -S git | debian: apt install git | alpine: apk add git",
		run:         lookPathWithVersion("git", "--version", ""),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "curl",
		Why:         "tarball fetch + HTTPS repo access",
		InstallHint: "arch: pacman -S curl | debian: apt install curl | alpine: apk add curl",
		run:         lookPathFor("curl"),
		HostOnly:    true,
	},
	{
		Group:       GroupCoreBuild,
		Name:        "tar",
		Why:         "tarball extract for rootfs + host-chroot bootstrap",
		InstallHint: "arch: pacman -S tar | debian: apt install tar | alpine: apk add tar",
		run:         lookPathFor("tar"),
		HostOnly:    true,
	},
	{
		Group:       GroupCoreBuild,
		Name:        "gzip",
		Why:         "rootfs + initramfs compression",
		InstallHint: "arch: pacman -S gzip | debian: apt install gzip | alpine: apk add gzip",
		run:         lookPathFor("gzip"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "xz",
		Why:         "kernel + some tarballs ship as .tar.xz",
		InstallHint: "arch: pacman -S xz | debian: apt install xz-utils | alpine: apk add xz",
		run:         lookPathFor("xz"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "python3",
		Why:         "mkbootimg + kernel build scripts (>=3.11)",
		InstallHint: "arch: pacman -S python | debian: apt install python3 | alpine: apk add python3",
		run:         lookPathWithVersion("python3", "--version", "3.11"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "pkg-config",
		Why:         "library detection in ports' configure scripts",
		InstallHint: "arch: pacman -S pkgconf | debian: apt install pkg-config | alpine: apk add pkgconf",
		run:         lookPathFor("pkg-config"),
	},
	{
		Group:       GroupCoreBuild,
		Name:        "peacock-ports",
		Why:         "device + base port manifests the build reads",
		InstallHint: "run `peacock build` to fetch automatically, or set PEACOCK_PORTS_DIR",
		run:         portsPresentRunner,
		HostOnly:    true,
	},

	// cross-arch
	{
		Group:                 GroupCrossArch,
		Name:                  "qemu-aarch64-static",
		Why:                   "needed to run aarch64 build steps from x86_64 host",
		InstallHint:           "arch: pacman -S qemu-user-static-binfmt (AUR: qemu-user-static-bin) | debian: apt install qemu-user-static | alpine: apk add qemu-aarch64",
		run:                   lookPathFor("qemu-aarch64-static"),
		CollapsedByHostChroot: true,
	},
	{
		Group:                 GroupCrossArch,
		Name:                  "qemu-arm-static",
		Why:                   "needed to run armv7 build steps from x86_64 host",
		InstallHint:           "arch: pacman -S qemu-user-static-binfmt | debian: apt install qemu-user-static | alpine: apk add qemu-arm",
		run:                   lookPathFor("qemu-arm-static"),
		CollapsedByHostChroot: true,
	},
	{
		Group:       GroupCrossArch,
		Name:        "binfmt_misc/qemu-aarch64",
		Why:         "kernel binfmt registration for transparent aarch64 exec",
		InstallHint: "systemctl restart systemd-binfmt (or modprobe binfmt_misc; echo registration to /proc/sys/fs/binfmt_misc/register)",
		run:         fileExistsRunner("/proc/sys/fs/binfmt_misc/qemu-aarch64"),
	},
	{
		Group:                 GroupCrossArch,
		Name:                  "aarch64-linux-gnu-gcc",
		Why:                   "aarch64 cross compiler (arch flavor)",
		InstallHint:           "arch: pacman -S aarch64-linux-gnu-gcc | debian: apt install gcc-aarch64-linux-gnu",
		Flavors:               []string{"arch"},
		run:                   lookPathFor("aarch64-linux-gnu-gcc"),
		CollapsedByHostChroot: true,
	},
	{
		Group:                 GroupCrossArch,
		Name:                  "debootstrap",
		Why:                   "debian flavor bootstrap (apt root)",
		InstallHint:           "arch: pacman -S debootstrap | debian: apt install debootstrap",
		Flavors:               []string{"debian"},
		run:                   lookPathFor("debootstrap"),
		CollapsedByHostChroot: true,
	},
	{
		Group:                 GroupCrossArch,
		Name:                  "qemu-user-static (debian)",
		Why:                   "qemu wrappers required by debootstrap second-stage on foreign arch",
		InstallHint:           "debian: apt install qemu-user-static | arch: pacman -S qemu-user-static-binfmt",
		Flavors:               []string{"debian"},
		run:                   lookPathFor("qemu-aarch64-static"),
		CollapsedByHostChroot: true,
	},
	{
		Group:                 GroupCrossArch,
		Name:                  "apk",
		Why:                   "alpine flavor bootstrap (apk-tools)",
		InstallHint:           "alpine: apk add apk-tools-static | arch (AUR): yay -S apk-tools-static | build from https://gitlab.alpinelinux.org/alpine/apk-tools",
		Flavors:               []string{"alpine"},
		run:                   lookPathAny("apk", "apk.static", "apk-tools-static"),
		CollapsedByHostChroot: true,
	},

	// bootloader
	{
		Group:       GroupBootloader,
		Name:        "arm-none-eabi-gcc",
		Why:         "lk2nd bootloader cross-compiler",
		InstallHint: "arch: pacman -S arm-none-eabi-gcc | debian: apt install gcc-arm-none-eabi | alpine: apk add gcc-arm-none-eabi",
		run:         lookPathFor("arm-none-eabi-gcc"),
	},
	{
		Group:       GroupBootloader,
		Name:        "arm-none-eabi-binutils",
		Why:         "lk2nd bootloader binutils (objcopy, ld)",
		InstallHint: "arch: pacman -S arm-none-eabi-binutils | debian: apt install binutils-arm-none-eabi",
		run:         lookPathFor("arm-none-eabi-ld"),
	},
	{
		Group:       GroupBootloader,
		Name:        "dtc",
		Why:         "device-tree compiler (kernel + lk2nd DTB)",
		InstallHint: "arch: pacman -S dtc | debian: apt install device-tree-compiler | alpine: apk add dtc",
		run:         lookPathFor("dtc"),
	},

	// device
	{
		Group:          GroupDevice,
		Name:           "fastboot",
		Why:            "flashing boot.img + partitions",
		InstallHint:    "arch: pacman -S android-tools | debian: apt install fastboot | alpine: apk add android-tools",
		DeviceFamilies: []string{"android", "fastboot", "oppo-a16", "samsung-jflte", "xiaomi-daisy"},
		run:            lookPathFor("fastboot"),
		HostOnly:       true,
	},
	{
		Group:          GroupDevice,
		Name:           "adb",
		Why:            "shell access + sideload + logcat",
		InstallHint:    "arch: pacman -S android-tools | debian: apt install adb | alpine: apk add android-tools",
		DeviceFamilies: []string{"android", "fastboot", "oppo-a16", "samsung-jflte", "xiaomi-daisy"},
		run:            lookPathFor("adb"),
		HostOnly:       true,
	},
	{
		Group:          GroupDevice,
		Name:           "mkbootimg",
		Why:            "Android boot.img assembly",
		InstallHint:    "arch: pacman -S android-tools (provides mkbootimg) | debian: apt install android-sdk-libsparse-utils mkbootimg | alpine: apk add mkbootimg",
		DeviceFamilies: []string{"android", "fastboot", "oppo-a16", "samsung-jflte", "xiaomi-daisy"},
		run:            lookPathFor("mkbootimg"),
	},
	{
		Group:          GroupDevice,
		Name:           "abootimg",
		Why:            "boot.img inspection helper",
		InstallHint:    "arch (AUR): yay -S abootimg | debian: apt install abootimg",
		DeviceFamilies: []string{"android", "fastboot", "oppo-a16", "samsung-jflte", "xiaomi-daisy"},
		run:            lookPathFor("abootimg"),
	},
	{
		Group:          GroupDevice,
		Name:           "unpack_bootimg",
		Why:            "Android boot.img unpack (shipped by android-tools)",
		InstallHint:    "arch: pacman -S android-tools | debian: apt install android-sdk-libsparse-utils",
		DeviceFamilies: []string{"android", "fastboot", "oppo-a16", "samsung-jflte", "xiaomi-daisy"},
		run:            lookPathFor("unpack_bootimg"),
	},

	// feather
	{
		Group:       GroupFeather,
		Name:        "ftr",
		Why:         "feather binary for /peacock + /apps overlay install (phase 4)",
		InstallHint: "build & install PeacockProject/feather, or `go install ...`. Falls back to /peacock/bin/ftr.",
		run:         ftrRunner,
		HostOnly:    true,
	},

	// filesystem / privileges
	{
		Group:       GroupFilesystem,
		Name:        "sudo (passwordless)",
		Why:         "build path uses `sudo -n` for chroot mounts + loop devices",
		InstallHint: "add a /etc/sudoers.d/ entry granting NOPASSWD for the build user, or run inside a privileged session",
		run:         sudoNoPrompt,
		HostOnly:    true,
	},
	{
		Group:       GroupFilesystem,
		Name:        "/dev/loop-control",
		Why:         "loop device support for image-builder",
		InstallHint: "modprobe loop, or boot a kernel with CONFIG_BLK_DEV_LOOP=y",
		run:         fileExistsRunner("/dev/loop-control"),
		HostOnly:    true,
	},
	{
		Group:       GroupFilesystem,
		Name:        "binfmt_misc",
		Why:         "kernel binfmt_misc subsystem (needed for qemu transparent exec)",
		InstallHint: "mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc, or enable CONFIG_BINFMT_MISC=y",
		run:         fileExistsRunner("/proc/sys/fs/binfmt_misc/status"),
	},
}

// HostChrootProbes appends the host-chroot-mode-specific probes when
// --use-host-chroot is in play. Kept separate so the always-on table
// stays small.
func hostChrootProbesFor(flavor string) []Probe {
	if flavor == "" {
		return nil
	}
	chroot := Probe{
		Group:       GroupHostChroot,
		Name:        "chroot",
		Why:         "needed to enter the host-chroot for --use-host-chroot mode",
		InstallHint: "arch: pacman -S coreutils | debian: apt install coreutils | alpine: apk add coreutils",
		run:         lookPathFor("chroot"),
		HostOnly:    true,
	}
	root := Probe{
		Group:       GroupHostChroot,
		Name:        fmt.Sprintf("host-chroot/%s", flavor),
		Why:         "rootfs for --use-host-chroot=" + flavor + " (auto-bootstrapped on first build)",
		InstallHint: "run `peacock build --use-host-chroot=" + flavor + " --device <dev>` to bootstrap automatically",
		run:         hostChrootRootRunner(flavor),
		HostOnly:    true,
	}
	return []Probe{chroot, root}
}

// FilterAndRunWithHostChroot wraps FilterAndRun and appends the
// host-chroot-only probes when opts.UseHostChroot is set.
func FilterAndRunWithHostChroot(opts ProbeOpts) []Result {
	results := FilterAndRun(opts)
	for _, p := range hostChrootProbesFor(opts.UseHostChroot) {
		results = append(results, p.Run(opts))
	}
	return results
}

// Summary counts the number of OK / Missing / Broken results.
type Summary struct {
	OK      int
	Missing int
	Broken  int
	Skipped int
}

// SummarizeResults walks results and tallies their statuses.
func SummarizeResults(results []Result) Summary {
	var s Summary
	for _, r := range results {
		switch r.Status {
		case StatusOK:
			s.OK++
		case StatusMissing:
			s.Missing++
		case StatusBroken:
			s.Broken++
		case StatusSkipped:
			s.Skipped++
		}
	}
	return s
}

// IsFatal reports whether the summary should yield a non-zero exit
// code (any Missing or Broken).
func (s Summary) IsFatal() bool {
	return s.Missing > 0 || s.Broken > 0
}

// silence the unused-warning for lookPath; kept for future per-probe
// overrides where the default runner is the right shape.
var _ = lookPath
var _ = filepath.Join
