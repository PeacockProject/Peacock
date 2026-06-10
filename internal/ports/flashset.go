package ports

// flashset.go derives a device's "flashable set" — the bootloader and
// recovery (PRP) ports, plus any PRP-specific kernel — by checking which
// port directories exist under peacock-ports/device/. No hardcoded
// table: a new device's flashable set lights up automatically once its
// ports land. The system rootfs/image is built by the main pipeline and
// is NOT part of this set.

import (
	"os"
	"path/filepath"

	"peacock/internal/manifest"
)

// FlashSet names the ports that make up a device's flashable artifacts,
// in the order they must build: a PRP-specific kernel (if the device has
// one) is needed before the PRP recovery image; the bootloader is
// independent. Empty fields mean "no such port for this device" (e.g.
// PinePhone/x86 have no MTK/qcom bootloader).
type FlashSet struct {
	Device     string // device codename
	PRPKernel  string // kernel port that yields a PRP kernel — built before Recovery
	Bootloader string // minkernel-<dev> | lk2nd-<dev> | ""
	Recovery   string // prp-<dev> | ""
}

// kernelHasPRPVariant reports whether the main kernel port for the device
// builds a PRP-trimmed second kernel (it sets [build].prp_kernel_config).
// Such a port stages a zImage-prp alongside the full zImage, so the
// recovery reuses it instead of a separate linux-<dev>-prp port.
func kernelHasPRPVariant(root, kernelPort string) bool {
	p := filepath.Join(root, "device", kernelPort, "package.toml")
	pkg, err := manifest.LoadPackage(p)
	if err != nil {
		return false
	}
	return pkg.Build.PRPKernelConfig != ""
}

// portExists reports whether device/<name>/package.toml is present under
// the resolved ports root.
func portExists(root, name string) bool {
	if root == "" {
		return false
	}
	_, err := os.Stat(filepath.Join(root, "device", name, "package.toml"))
	return err == nil
}

// ResolveFlashSet inspects peacock-ports for the device's bootloader,
// recovery, and PRP-kernel ports. It is read-only and never clones; call
// Ensure() first if the tree might be absent. found is false when the
// ports root can't be resolved at all.
func ResolveFlashSet(device string) (set FlashSet, found bool) {
	root, ok := Resolve()
	if !ok {
		return FlashSet{Device: device}, false
	}
	set.Device = device

	// PRP kernel: the main kernel port builds a PRP-trimmed variant in the
	// same source tree (it declares prp_kernel_config) and stages it as
	// zImage-prp. No separate linux-<dev>-prp port — that pattern is gone.
	if name := "linux-" + device; kernelHasPRPVariant(root, name) {
		set.PRPKernel = name
	}

	// Bootloader: MTK devices use minkernel, qcom use lk2nd. Prefer
	// minkernel when both somehow exist (MTK is the in-tree default).
	if name := "minkernel-" + device; portExists(root, name) {
		set.Bootloader = name
	} else if name := "lk2nd-" + device; portExists(root, name) {
		set.Bootloader = name
	}

	// Recovery (PRP) image.
	if name := "prp-" + device; portExists(root, name) {
		set.Recovery = name
	}

	return set, true
}

// Empty reports whether the device has no bootloader and no recovery —
// i.e. nothing flashable beyond the system image (PinePhone, x86, …).
func (s FlashSet) Empty() bool {
	return s.Bootloader == "" && s.Recovery == ""
}
