package pipeline

// Phase 4 of the build pipeline. Builds (or extracts) the kernel,
// stands up the image-build chroot, populates the rootfs with the
// resolved package list, applies init-system / desktop / display-manager
// configuration tweaks, extracts kernel modules into the rootfs, and
// stages the extlinux boot assets under /boot.

import (
	"fmt"
	"os/exec"
	"path/filepath"
	"strings"

	"peacock/internal/builder"
	"peacock/internal/chroot"
	"peacock/internal/config"
	"peacock/internal/manifest"
	"peacock/internal/runner"
	"peacock/internal/userland"
)

// rootfsPhaseResult collects everything phase 4 produces and that
// phase 5 (image assembly) needs.
type rootfsPhaseResult struct {
	imageChrootRoot string
	rootfsPath      string
	kernelBuildDir  string
	kernelImagePath string
}

// runRootfsPhase performs phase 4. The cleanup arg is mutated to register
// the image-build chroot so signal handlers / deferred Run() can unmount
// it. Returns the locations needed by image assembly.
func (r *Runner) runRootfsPhase(
	b *builder.Builder,
	pkg *manifest.Package,
	dev *manifest.Device,
	depBuildDirs map[string]string,
	depPackagePaths map[string]string,
	pkgs []string,
	localPackages []string,
	cacheDir string,
	initSystem string,
	desktopChoice string,
	displayManagerChoice string,
	userName string,
	userPassword string,
	emptyRootfs bool,
	initramfsPath string,
	workDir string,
	cleanup *Cleanup,
) (*rootfsPhaseResult, error) {
	_ = pkg // currently unused; reserved for future per-package rootfs hooks
	useQemuFlag := r.opts.UseQemu
	crossCompileFlag := r.opts.CrossCompile
	deviceName := r.opts.Device
	res := &rootfsPhaseResult{}

	// 8. Build Kernel
	runner.Logln("Building/Fetching Kernel...")
	kernelManifest := filepath.Join(portsRoot, "device", "linux-"+deviceName, "package.toml")
	kernelPkg, err := manifest.LoadPackage(kernelManifest)
	if err != nil {
		runner.Logf("Kernel manifest not found: %v. Skipping boot.img\n", err)
	} else {
		kernelOpts, kernelChrootArch, err := resolveBuildOptions(kernelPkg, dev.Device.Architecture, useQemuFlag, crossCompileFlag)
		if err != nil {
			return nil, fmt.Errorf("error resolving build options for kernel: %w", err)
		}
		if cachedDir, ok := depBuildDirs[kernelPkg.Package.Name]; ok {
			zImagePath := filepath.Join(cachedDir, "zImage")
			if fileExistsFile(zImagePath) {
				res.kernelBuildDir = cachedDir
				runner.Logf("Reusing kernel build from dependencies at %s\n", res.kernelBuildDir)
			}
		}
		if res.kernelBuildDir == "" {
			if pkgPath, ok := depPackagePaths[kernelPkg.Package.Name]; ok {
				extractedDir, err := extractKernelFromPackage(pkgPath, workDir)
				if err != nil {
					return nil, fmt.Errorf("error extracting kernel from cached package: %w", err)
				}
				res.kernelBuildDir = extractedDir
				runner.Logf("Reusing kernel extracted from cached package at %s\n", res.kernelBuildDir)
			}
		}
		if res.kernelBuildDir == "" {
			runner.Logln("Kernel not built in dependencies; building now...")
			kernelChrootDir := filepath.Join(workDir, "build-chroot", kernelChrootArch)
			buildDepChrootRoot := filepath.Join(workDir, "build-dep-chroot", builder.HostArchString())
			kernelUseQemu := kernelOpts.UseQemu != nil && *kernelOpts.UseQemu
			if err := b.EnsureBuildChroot(kernelChrootDir, kernelChrootArch, kernelUseQemu); err != nil {
				return nil, fmt.Errorf("error ensuring build chroot for kernel: %w", err)
			}
			if err := ensureBuildChrootBootstrap(b, kernelChrootDir, kernelChrootArch); err != nil {
				return nil, fmt.Errorf("error bootstrapping build tools for kernel: %w", err)
			}
			kernelExtraPaths, err := prepareBuildDepPackages(b, kernelPkg, dev.Device.Architecture, kernelChrootDir, buildDepChrootRoot)
			if err != nil {
				return nil, fmt.Errorf("error preparing build dep packages for kernel: %w", err)
			}
			kernelOpts.ExtraPath = kernelExtraPaths.Bin
			kernelOpts.ExtraInclude = kernelExtraPaths.Inc
			kernelOpts.ExtraLib = kernelExtraPaths.Lib
			kernelOpts.ExtraLdLib = kernelExtraPaths.LD
			res.kernelBuildDir, err = b.BuildPackageInChroot(kernelPkg, dev.Device.Architecture, kernelChrootDir, kernelOpts)
			if err != nil {
				return nil, fmt.Errorf("error building kernel: %w", err)
			}
		}
		res.kernelImagePath = filepath.Join(res.kernelBuildDir, "zImage")
		if !fileExistsFile(res.kernelImagePath) {
			runner.Logf("Warning: kernel image not found at %s\n", res.kernelImagePath)
			res.kernelImagePath = ""
		}
	}

	// 9. Create Image using dedicated image-build-chroot
	runner.Logln("=== Phase 2: Image Assembly ===")

	runner.Logln("Setting up image build environment...")
	imageChrootRoot, err := b.EnsureImageBuildChroot()
	if err != nil {
		return nil, fmt.Errorf("error preparing image build chroot: %w", err)
	}
	res.imageChrootRoot = imageChrootRoot

	cleanup.imageChroot = imageChrootRoot

	// Boot-model flip: the image root is the Peacock BASE (LABEL=ROOT, PID 1 =
	// peacock-init). The flavor distro is installed UNDER /flavors/<name> and
	// entered by peacock-init at boot. Pointing rootfsPath at the flavor subdir
	// keeps the entire flavor / init-config block below unchanged — it already
	// operates on rootfsPath.
	baseRoot := filepath.Join(imageChrootRoot, "rootfs")
	flavor := config.Flavor()
	if flavor == "" {
		flavor = "arch"
	}
	rootfsPath := filepath.Join(baseRoot, "flavors", flavor)
	res.rootfsPath = baseRoot // image assembly copies the whole base (incl. /flavors)
	if err := unmountRootfsSubmounts(baseRoot); err != nil {
		runner.Logf("Warning: failed to unmount stale rootfs submounts: %v\n", err)
	}
	_ = chroot.UnmountPathWithSudo(baseRoot)
	if err := execCommand("sudo", "rm", "-rf", "--one-file-system", baseRoot); err != nil {
		runner.Logf("Warning: failed to clean rootfs: %v\n", err)
	}
	if err := execCommand("sudo", "mkdir", "-p", rootfsPath); err != nil {
		runner.Logf("Warning: failed to create flavor rootfs: %v\n", err)
	}

	// Determine packages to install
	allPackages := pkgs

	if !emptyRootfs {
		if len(localPackages) > 0 {
			for _, pkgPath := range localPackages {
				dst := filepath.Join(cacheDir, filepath.Base(pkgPath))
				if err := execCommand("cp", "-f", pkgPath, dst); err != nil {
					runner.Logf("Warning: failed to copy package %s to cache: %v\n", pkgPath, err)
				}
			}
		}

		runner.Logln("Installing packages to rootfs...")
		if err := b.InstallPackagesToRootfs(imageChrootRoot, rootfsPath, allPackages, dev.Device.Architecture); err != nil {
			return nil, fmt.Errorf("error installing packages to rootfs: %w", err)
		}
		if userName != "" {
			if err := b.CreateUserInRootfs(imageChrootRoot, rootfsPath, userName, userPassword); err != nil {
				return nil, fmt.Errorf("error creating user '%s': %w", userName, err)
			}
		}
	} else {
		runner.Logln("Skipping package installation into rootfs (empty-rootfs mode)")
	}
	if initSystem == "openrc" && !emptyRootfs {
		rcConfPath := filepath.Join(rootfsPath, "etc", "rc.conf")
		_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
RC="%s"
tmp="$(mktemp)"
if [ -f "$RC" ]; then
	grep -vE '^(#?rc_logger=|#?rc_log_path=)' "$RC" > "$tmp"
else
	: > "$tmp"
fi
printf 'rc_logger="YES"\nrc_log_path="/var/log/rc.log"\n' >> "$tmp"
mv "$tmp" "$RC"
`, rcConfPath))

		dmService := userland.DisplayManagerService(displayManagerChoice)
		if dmService != "" {
			if err := b.EnableOpenRCService(imageChrootRoot, rootfsPath, dmService, "default"); err != nil {
				return nil, fmt.Errorf("error enabling display manager '%s' in openrc: %w", dmService, err)
			}
			_ = execCommand("sudo", "rm", "-f", filepath.Join(rootfsPath, "etc", "runlevels", "default", "agetty.tty1"))
		}

		if err := b.EnableOpenRCService(imageChrootRoot, rootfsPath, "devfs", "boot"); err != nil {
			return nil, fmt.Errorf("enable devfs (openrc): %w", err)
		}
		_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
ROOT="%s"
cat > "$ROOT/etc/init.d/run-tmpfs" <<'EOF'
#!/sbin/openrc-run

description="Mount /run tmpfs"

depend() {
	need localmount
	before dbus
}

start() {
	checkpath -d -m 0755 /run
	if ! grep -q ' /run ' /proc/mounts; then
		mount -t tmpfs -o mode=0755,nosuid,nodev tmpfs /run
	fi
	checkpath -d -m 0755 /run/dbus
}
EOF
chmod 755 "$ROOT/etc/init.d/run-tmpfs"
`, rootfsPath))
		if err := b.EnableOpenRCService(imageChrootRoot, rootfsPath, "run-tmpfs", "boot"); err != nil {
			return nil, fmt.Errorf("enable run-tmpfs (openrc): %w", err)
		}

		// Boot-ready signal for the Peacock base supervisor: once the flavor
		// reaches its default runlevel it touches /peacock/.flavor-ready (a
		// base-owned path bind-mounted in, so the flavor's own /run tmpfs can't
		// shadow it). peacock-init watches this to tell "booted OK" from "hung".
		_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
ROOT="%s"
cat > "$ROOT/etc/init.d/peacock-flavor-ready" <<'EOF'
#!/sbin/openrc-run

description="Signal the Peacock base that this flavor finished booting"

depend() {
	after *
}

start() {
	: > /peacock/.flavor-ready 2>/dev/null || true
}
EOF
chmod 755 "$ROOT/etc/init.d/peacock-flavor-ready"
`, rootfsPath))
		if err := b.EnableOpenRCService(imageChrootRoot, rootfsPath, "peacock-flavor-ready", "default"); err != nil {
			return nil, fmt.Errorf("enable peacock-flavor-ready (openrc): %w", err)
		}

		extraServices := userland.DisplayManagerOpenRCServices(displayManagerChoice, initSystem)
		for _, svc := range extraServices {
			if err := b.EnableOpenRCService(imageChrootRoot, rootfsPath, svc.Name, svc.Runlevel); err != nil {
				return nil, fmt.Errorf("error enabling openrc service '%s' in runlevel '%s': %w", svc.Name, svc.Runlevel, err)
			}
		}

		if strings.ToLower(displayManagerChoice) == "sddm" {
			minimumVT := "7"
			if initSystem == "openrc" {
				minimumVT = "1"
			}
			serverPath := "/usr/lib/Xorg"
			serverArguments := "-nolisten tcp -noreset -verbose 4 -logfile /var/log/Xorg.0.log"
			if dev.Quirks.XorgForceVT1 {
				minimumVT = "1"
				serverPath = "/usr/local/sbin/peacock-xorg-vt1"
			}
			_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
ROOT="%s"
mkdir -p "$ROOT/etc" "$ROOT/var/log" "$ROOT/var/run" "$ROOT/var/lib"
if ! grep -q '^video:' "$ROOT/etc/group"; then echo 'video:x:27:' >> "$ROOT/etc/group"; fi
if ! grep -q '^input:' "$ROOT/etc/group"; then echo 'input:x:24:' >> "$ROOT/etc/group"; fi
if ! grep -q '^sddm:' "$ROOT/etc/group"; then echo 'sddm:x:965:' >> "$ROOT/etc/group"; fi
if ! grep -q '^sddm:' "$ROOT/etc/passwd"; then echo 'sddm:x:965:965:Simple Desktop Display Manager:/var/lib/sddm:/usr/bin/nologin' >> "$ROOT/etc/passwd"; fi
if [ -f "$ROOT/etc/shadow" ] && ! grep -q '^sddm:' "$ROOT/etc/shadow"; then echo 'sddm:!*:::::::' >> "$ROOT/etc/shadow"; fi
if [ -f "$ROOT/etc/gshadow" ] && ! grep -q '^sddm:' "$ROOT/etc/gshadow"; then echo 'sddm:!*::' >> "$ROOT/etc/gshadow"; fi
for grp in video input; do
	line="$(awk -F: -v g="$grp" '$1==g{print; exit}' "$ROOT/etc/group" 2>/dev/null || true)"
	[ -n "$line" ] || continue
	members="$(echo "$line" | cut -d: -f4)"
	case ",$members," in
		*,sddm,*) ;;
		*) new_members="${members:+$members,}sddm"
		   awk -F: -v OFS=: -v g="$grp" -v m="$new_members" '$1==g{$4=m} {print}' "$ROOT/etc/group" > "$ROOT/etc/group.tmp"
		   mv "$ROOT/etc/group.tmp" "$ROOT/etc/group" ;;
	esac
done
mkdir -p "$ROOT/var/lib/sddm/.local/share/sddm" "$ROOT/var/run/sddm" "$ROOT/var/log"
sddm_uid="$(awk -F: '$1=="sddm"{print $3; exit}' "$ROOT/etc/passwd" 2>/dev/null || true)"
sddm_gid="$(awk -F: '$1=="sddm"{print $3; exit}' "$ROOT/etc/group" 2>/dev/null || true)"
[ -n "$sddm_uid" ] || sddm_uid=965
[ -n "$sddm_gid" ] || sddm_gid=965
chown -R "$sddm_uid:$sddm_gid" "$ROOT/var/lib/sddm" "$ROOT/var/run/sddm" || true
chmod 0755 "$ROOT/var/lib/sddm" "$ROOT/var/lib/sddm/.local" "$ROOT/var/lib/sddm/.local/share" "$ROOT/var/lib/sddm/.local/share/sddm" "$ROOT/var/run/sddm"
: > "$ROOT/var/log/sddm.log"
chown "$sddm_uid:$sddm_gid" "$ROOT/var/log/sddm.log" || true
chmod 0666 "$ROOT/var/log/sddm.log"
mkdir -p "$ROOT/etc/sddm.conf.d"
cat > "$ROOT/usr/bin/peacock-sddm-greeter" <<'EOF'
#!/bin/sh
# Prefer greeter matching the Qt major of the SDDM daemon/helper when available.
if [ -x /usr/bin/sddm-greeter-qt6 ]; then
	exec /usr/bin/sddm-greeter-qt6 "$@"
fi
exec /usr/bin/sddm-greeter "$@"
EOF
chmod 0755 "$ROOT/usr/bin/peacock-sddm-greeter"
# Older SDDM builds ignore GreeterPath and always call /usr/bin/sddm-greeter.
# On those systems, force the default greeter entrypoint to Qt6 when available.
if [ -x "$ROOT/usr/bin/sddm-greeter-qt6" ]; then
	cat > "$ROOT/usr/bin/sddm-greeter" <<'EOF'
#!/bin/sh
exec /usr/bin/sddm-greeter-qt6 "$@"
EOF
	chmod 0755 "$ROOT/usr/bin/sddm-greeter"
fi
if [ "%s" = "/usr/local/sbin/peacock-xorg-vt1" ]; then
	mkdir -p "$ROOT/usr/local/sbin"
	cat > "$ROOT/usr/local/sbin/peacock-xorg-vt1" <<'EOF'
#!/bin/bash
set -euo pipefail
args=()
for a in "$@"; do
	if [[ "$a" =~ ^vt[0-9]+$ ]]; then
		continue
	fi
	args+=("$a")
done
exec /usr/lib/Xorg "${args[@]}" -keeptty vt1
EOF
	chmod 0755 "$ROOT/usr/local/sbin/peacock-xorg-vt1"
fi
cat > "$ROOT/etc/sddm.conf.d/peacock.conf" <<'EOF'
[General]
LogFile=/var/log/sddm.log
MinimumVT=%s
DisplayServer=x11
InputMethod=qtvirtualkeyboard
GreeterPath=/usr/bin/peacock-sddm-greeter
GreeterEnvironment=QT_QUICK_BACKEND=software,QSG_RHI_BACKEND=software,QT_XCB_NO_XI2=1,QT_IM_MODULE=qtvirtualkeyboard

[Theme]
Current=maldives

[X11]
ServerPath=%s
ServerArguments=%s
EnableHiDPI=false
EOF
mkdir -p "$ROOT/etc/X11"
cat > "$ROOT/etc/X11/Xwrapper.config" <<'EOF'
allowed_users=anybody
needs_root_rights=yes
EOF
`, rootfsPath, serverPath, minimumVT, serverPath, serverArguments))
		}
	}

	// Install kernel modules if available
	if res.kernelBuildDir != "" {
		modulesTarPath := filepath.Join(res.kernelBuildDir, "modules.tar.gz")
		if fileExistsFile(modulesTarPath) {
			runner.Logln("Extracting kernel modules to rootfs...")
			extractCmd := exec.Command("sudo", "tar", "-xzf", modulesTarPath, "-C", rootfsPath)
			if err := runner.RunCmd(extractCmd); err != nil {
				runner.Logf("Warning: failed to extract kernel modules: %v\n", err)
			}
		}
	}

	if initSystem == "openrc" {
		_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
ROOT="%s"
if [ ! -f "$ROOT/etc/inittab" ]; then
	mkdir -p "$ROOT/etc"
	cat > "$ROOT/etc/inittab" <<'EOF'
::sysinit:/sbin/openrc sysinit
::wait:/sbin/openrc boot
::wait:/sbin/openrc default
::ctrlaltdel:/sbin/openrc reboot
::shutdown:/sbin/openrc shutdown
tty1::respawn:/sbin/agetty -L 115200 tty1 vt100
EOF
fi
`, rootfsPath))
		if strings.ToLower(displayManagerChoice) != "none" {
			_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
ROOT="%s"
if [ -f "$ROOT/etc/inittab" ]; then
	sed -i '/^tty1::respawn:/d' "$ROOT/etc/inittab"
	sed -i '/^tty2::respawn:/d' "$ROOT/etc/inittab"
	sed -i 's|^tty3::respawn:.*|tty3::respawn:/sbin/agetty -L 115200 tty3 vt100|' "$ROOT/etc/inittab"
	if ! grep -q '^tty3::respawn:' "$ROOT/etc/inittab"; then
		echo 'tty3::respawn:/sbin/agetty -L 115200 tty3 vt100' >> "$ROOT/etc/inittab"
	fi
fi
`, rootfsPath))
		}
		_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
ROOT="%s"
mkdir -p "$ROOT/etc/conf.d"
if [ ! -f "$ROOT/etc/conf.d/devfs" ]; then
	echo 'skip_mount_dev=yes' > "$ROOT/etc/conf.d/devfs"
elif ! grep -q '^skip_mount_dev=' "$ROOT/etc/conf.d/devfs"; then
	echo 'skip_mount_dev=yes' >> "$ROOT/etc/conf.d/devfs"
else
	sed -i 's/^skip_mount_dev=.*/skip_mount_dev=yes/' "$ROOT/etc/conf.d/devfs"
fi
`, rootfsPath))

		_ = execCommand("sudo", "sh", "-c", fmt.Sprintf(`set -e
CFG="%s/etc/mkinitcpio.conf"
if [ -f "$CFG" ]; then
	sed -i -E 's|^HOOKS=.*|HOOKS=(base udev autodetect microcode modconf kms keyboard keymap consolefont block filesystems fsck)|' "$CFG"
	# Force common virtual/disk drivers in. autodetect trims the initramfs to the
	# BUILD HOST's loaded modules, so a flavor initramfs booted on different
	# hardware (e.g. the qemu-x86_64 target, which GRUB boots via this mkinitcpio
	# initramfs instead of peacock-mkinitfs) couldn't find its root disk
	# ("device 'LABEL=ROOT' not found"). Small; harmless where unused.
	sed -i -E 's|^MODULES=.*|MODULES=(virtio_blk virtio_pci virtio_scsi virtio_net ahci ata_piix sd_mod sr_mod nvme)|' "$CFG"
fi
`, rootfsPath))
		if err := chroot.MountWithSudo(rootfsPath); err != nil {
			return nil, fmt.Errorf("error mounting rootfs for mkinitcpio regeneration: %w", err)
		}
		var regenErr error
		func() {
			defer chroot.UnmountWithSudo(rootfsPath)
			if err := execCommand("sudo", "chroot", rootfsPath, "sh", "-lc", "command -v mkinitcpio >/dev/null 2>&1"); err != nil {
				runner.Logln("Warning: mkinitcpio not found in rootfs; skipping rootfs initramfs regeneration")
				return
			}
			if err := execCommand("sudo", "chroot", rootfsPath, "mkinitcpio", "-P"); err != nil {
				regenErr = fmt.Errorf("error regenerating rootfs initramfs for openrc: %w", err)
				return
			}
		}()
		if regenErr != nil {
			return nil, regenErr
		}
	}

	// Assemble the Peacock BASE at the image root: peacock-init (PID 1) +
	// busybox, the base-owned /peacock /apps /compat /data trees, and the
	// active-flavor pointer. The base is what boots; peacock-init enters
	// /flavors/<flavor> from here.
	if !emptyRootfs {
		if err := r.assemblePeacockBase(b, imageChrootRoot, baseRoot, flavor, dev.Device.Architecture, workDir); err != nil {
			return nil, fmt.Errorf("error assembling peacock base: %w", err)
		}
	}

	if res.kernelImagePath != "" && fileExistsFile(initramfsPath) {
		dtbPath := discoverKernelDTB(res.kernelBuildDir, deviceName)
		runner.Logln("Staging extlinux boot assets into base /boot...")
		if err := stageExtlinuxBootAssets(baseRoot, res.kernelImagePath, initramfsPath, dev.Boot.Cmdline, dtbPath); err != nil {
			return nil, fmt.Errorf("error staging extlinux boot assets: %w", err)
		}
	} else {
		runner.Logln("Warning: skipping extlinux boot asset staging (missing kernel or initramfs)")
	}

	return res, nil
}

// assemblePeacockBase lays the Peacock BASE over the image root: it builds +
// installs peacock-init (the new /sbin/init / PID 1) and busybox, creates the
// base-owned /peacock /apps /compat /data trees, and records the active flavor.
// The base is the new LABEL=ROOT; at boot peacock-init enters /flavors/<flavor>.
func (r *Runner) assemblePeacockBase(b *builder.Builder, imageChrootRoot, baseRoot, flavor, arch, workDir string) error {
	runner.Logln("Assembling Peacock base (peacock-init + busybox + /peacock)...")

	// Ensure peacock-init is built so its .feather lands in the store (busybox
	// is already built for the initramfs). We only need the side effect.
	if _, err := buildPortForInitramfs(b, "peacock-init", arch, workDir, r.opts.UseQemu, r.opts.CrossCompile); err != nil {
		return fmt.Errorf("building peacock-init: %w", err)
	}

	// Install the base packages at the image root. InstallPackagesToRootfs
	// resolves each to its .feather and ftr-installs it: peacock-init ships
	// /sbin/peacock-init + the /sbin/init symlink, busybox ships /usr/bin/busybox.
	if err := b.InstallPackagesToRootfs(imageChrootRoot, baseRoot, []string{"busybox", "peacock-init"}, arch); err != nil {
		return fmt.Errorf("installing base packages: %w", err)
	}

	// Base-owned trees, the active-flavor pointer, and a /bin/sh for
	// peacock-init's phase-1 emergency shell — written with the image chroot's
	// privileges.
	script := fmt.Sprintf(`set -e
ROOT=%q
FLAVOR=%q
mkdir -p "$ROOT/peacock/etc" "$ROOT/apps" "$ROOT/compat" "$ROOT/data" "$ROOT/proc" "$ROOT/sys" "$ROOT/dev" "$ROOT/run" "$ROOT/bin" "$ROOT/sbin"
printf '%%s\n' "$FLAVOR" > "$ROOT/peacock/etc/active-flavor"
if [ -e "$ROOT/usr/bin/busybox" ] && [ ! -e "$ROOT/bin/sh" ]; then
	ln -sf /usr/bin/busybox "$ROOT/bin/sh"
fi
if [ -e "$ROOT/sbin/peacock-init" ] && [ ! -e "$ROOT/sbin/init" ]; then
	ln -sf peacock-init "$ROOT/sbin/init"
fi
`, baseRoot, flavor)
	if err := execCommand("sudo", "sh", "-c", script); err != nil {
		return fmt.Errorf("writing base layout: %w", err)
	}
	runner.Logf("Peacock base assembled; active flavor = %s\n", flavor)
	return nil
}
