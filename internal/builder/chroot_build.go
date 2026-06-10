package builder

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"

	"peacock/internal/chroot"
	"peacock/internal/manifest"
	"peacock/internal/runner"
)

const (
	archBootstrapURL = "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
	archArmRootfsURL = "https://archlinuxarm.org/os"
)

type BuildOptions struct {
	UseQemu      *bool
	CrossCompile string
	ExtraPath    []string
	ExtraInclude []string
	ExtraLib     []string
	ExtraLdLib   []string
	// Flavor selects the per-flavor build_deps alias table used to
	// rewrite Arch package names into the equivalent debian / alpine
	// names. Empty string means "arch" for back-compat with callers
	// that haven't been flavor-ified yet.
	Flavor string
}

func archRootfsURL(targetArch string) (string, error) {
	switch targetArch {
	case "x86_64":
		return archBootstrapURL, nil
	case "armv7", "armv7h":
		return fmt.Sprintf("%s/ArchLinuxARM-armv7-latest.tar.gz", archArmRootfsURL), nil
	case "aarch64":
		return fmt.Sprintf("%s/ArchLinuxARM-aarch64-latest.tar.gz", archArmRootfsURL), nil
	default:
		return "", fmt.Errorf("unsupported target arch for arch rootfs: %s", targetArch)
	}
}

func qemuStaticName(targetArch string) string {
	switch targetArch {
	case "armv7", "armv7h":
		return "qemu-arm-static"
	case "aarch64":
		return "qemu-aarch64-static"
	default:
		return ""
	}
}

// HostArchString returns the pacman-style architecture name for the host
// (e.g. x86_64, aarch64).
func HostArchString() string {
	switch runtime.GOARCH {
	case "amd64":
		return "x86_64"
	case "arm64":
		return "aarch64"
	case "arm":
		return "armv7"
	default:
		return runtime.GOARCH
	}
}

func copyFile(src, dst string, mode os.FileMode) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	if _, err := io.Copy(out, in); err != nil {
		return err
	}
	return os.Chmod(dst, mode)
}

func copyFileWithSudo(src, dst string) error {
	mkCmd := exec.Command("sudo", "mkdir", "-p", filepath.Dir(dst))
	mkCmd.Stdout = runner.LogWriter()
	mkCmd.Stderr = runner.LogWriter()
	if err := runner.RunCmd(mkCmd); err != nil {
		return err
	}
	cpCmd := exec.Command("sudo", "cp", "-f", src, dst)
	cpCmd.Stdout = runner.LogWriter()
	cpCmd.Stderr = runner.LogWriter()
	return runner.RunCmd(cpCmd)
}

// BuildPackageInChroot downloads and builds a package inside the given chroot.
func (b *Builder) BuildPackageInChroot(pkg *manifest.Package, targetArch string, root string, opts BuildOptions) (string, error) {
	if pkg.Build.Source == "" && pkg.Build.Script == "" {
		return "", fmt.Errorf("package %s has no source or build script", pkg.Package.Name)
	}

	useQemu := false
	if opts.UseQemu != nil {
		useQemu = *opts.UseQemu
	}

	hostArch := HostArchString()
	if !useQemu && opts.CrossCompile == "" && targetArch != hostArch {
		return "", fmt.Errorf("cross-arch build requires qemu or cross compiler; set build.cross_compile or --cross-compile, or enable qemu")
	}

	chrootArch := targetArch
	if !useQemu {
		chrootArch = hostArch
	}

	if err := b.EnsureBuildChroot(root, chrootArch, useQemu); err != nil {
		return "", err
	}

	masterRoot := ""
	// If EnsureBuildChroot created a nested environment, we know master is at ../x86_64
	if useQemu && ((hostArch == "amd64" && chrootArch != "x86_64") || (hostArch == "arm64" && chrootArch != "aarch64")) {
		// convention used in EnsureBuildChroot
		masterRoot = filepath.Join(filepath.Dir(root), "x86_64")
	}

	if err := chroot.MountWithSudo(root); err != nil {
		return "", err
	}
	defer chroot.UnmountWithSudo(root)

	if data, err := os.ReadFile("/etc/resolv.conf"); err == nil {
		_ = os.WriteFile(filepath.Join(root, "etc", "resolv.conf"), data, 0644)
	}

	// Cross-toolchain injection: when this port builds in cross mode
	// (use_qemu=false) for a foreign target, it needs a cross compiler in
	// the host-arch chroot. Rather than naming distro-specific cross
	// packages in the port (aarch64-linux-gnu-gcc only exists on Arch-x86,
	// breaks everywhere else), the port declares target_arch and we inject
	// an abstract `gcc-<target_arch>` dep that the per-flavor alias table
	// expands to the right packages for the active distro. In qemu/native
	// mode the chroot is the target arch and base-devel suffices, so no
	// injection.
	deps := pkg.Build.BuildDeps
	crossing := opts.UseQemu != nil && !*opts.UseQemu
	if crossing && pkg.Build.TargetArch != "" && pkg.Build.TargetArch != HostArchString() {
		deps = append([]string{"gcc-" + pkg.Build.TargetArch}, deps...)
	}
	resolvedDeps := ResolveBuildDeps(deps, opts.Flavor)
	if err := b.installBuildDeps(root, resolvedDeps, masterRoot); err != nil {
		return "", fmt.Errorf("failed to install build deps: %w", err)
	}

	tarball := ""
	if pkg.Build.Source != "" {
		var err error
		tarball, err = b.Download(pkg.Build.Source, pkg.Build.Checksum)
		if err != nil {
			return "", fmt.Errorf("failed to download source: %w", err)
		}
	}

	buildDir := filepath.Join(root, "build", pkg.Package.Name+"-"+pkg.Package.Version+"-"+targetArch)
	rmBuild := exec.Command("sudo", "rm", "-rf", buildDir)
	rmBuild.Stdout = runner.LogWriter()
	rmBuild.Stderr = runner.LogWriter()
	if err := runner.RunCmd(rmBuild); err != nil {
		return "", err
	}
	mkBuild := exec.Command("sudo", "mkdir", "-p", buildDir)
	mkBuild.Stdout = runner.LogWriter()
	mkBuild.Stderr = runner.LogWriter()
	if err := runner.RunCmd(mkBuild); err != nil {
		return "", err
	}
	chownBuild := exec.Command("sudo", "chown", "-R", fmt.Sprintf("%d:%d", os.Getuid(), os.Getgid()), buildDir)
	chownBuild.Stdout = runner.LogWriter()
	chownBuild.Stderr = runner.LogWriter()
	if err := runner.RunCmd(chownBuild); err != nil {
		return "", err
	}

	// Copy auxiliary files (config, patches) from package directory
	if pkg.ManifestDir != "" {
		files, err := os.ReadDir(pkg.ManifestDir)
		if err == nil {
			for _, file := range files {
				if file.Name() == "package.toml" {
					continue
				}
				srcFile := filepath.Join(pkg.ManifestDir, file.Name())
				destFile := filepath.Join(buildDir, file.Name())

				in, err := os.Open(srcFile)
				if err != nil {
					continue
				}

				out, err := os.Create(destFile)
				if err != nil {
					in.Close()
					continue
				}

				io.Copy(out, in)
				in.Close()
				out.Close()
			}
		}
	}

	runner.Logf("Building package %s %s for %s in %s (chroot)\n", pkg.Package.Name, pkg.Package.Version, targetArch, buildDir)

	// Extract source if provided
	if tarball != "" {
		cmd := exec.Command("tar", "-xf", tarball, "-C", buildDir, "--strip-components=1")
		cmd.Stdout = runner.LogWriter()
		cmd.Stderr = runner.LogWriter()
		if err := runner.RunCmd(cmd); err != nil {
			return "", fmt.Errorf("failed to extract source: %w", err)
		}
	}

	if pkg.Build.Script != "" {
		scriptPath := filepath.Join(buildDir, "peacock-build.sh")
		scriptContent := "#!/bin/sh\nset -e\n" + pkg.Build.Script + "\n"
		if err := os.WriteFile(scriptPath, []byte(scriptContent), 0755); err != nil {
			return "", err
		}

		buildDirInChroot := filepath.Join("/build", pkg.Package.Name+"-"+pkg.Package.Version+"-"+targetArch)
		archEnv := ""
		switch targetArch {
		case "armv7", "armv7h":
			archEnv = "arm"
		case "aarch64":
			archEnv = "arm64"
		case "x86_64":
			archEnv = "x86_64"
		}
		pathPrefix := ""
		if len(opts.ExtraPath) > 0 {
			pathPrefix = strings.Join(opts.ExtraPath, ":") + ":"
		}
		pathSuffix := ""
		if pathPrefix != "" {
			pathSuffix = ":" + pathPrefix
		}
		envLines := []string{
			"export PATH=/usr/sbin:/usr/bin:/sbin:/bin" + pathSuffix + "/usr/local/sbin:/usr/local/bin",
		}
		if len(opts.ExtraInclude) > 0 && opts.CrossCompile == "" {
			envLines = append(envLines, "export C_INCLUDE_PATH="+strings.Join(opts.ExtraInclude, ":")+":${C_INCLUDE_PATH:-}")
		}
		if len(opts.ExtraLib) > 0 && opts.CrossCompile == "" {
			envLines = append(envLines, "export LIBRARY_PATH="+strings.Join(opts.ExtraLib, ":")+":${LIBRARY_PATH:-}")
		}
		if len(opts.ExtraLdLib) > 0 && opts.CrossCompile == "" {
			envLines = append(envLines, "export LD_LIBRARY_PATH="+strings.Join(opts.ExtraLdLib, ":")+":${LD_LIBRARY_PATH:-}")
		}
		if archEnv != "" {
			envLines = append(envLines, "export ARCH="+archEnv)
		}
		if opts.CrossCompile != "" {
			envLines = append(envLines, "export CROSS_COMPILE="+opts.CrossCompile)
		}
		if jobsStr := strings.TrimSpace(os.Getenv("PEACOCK_JOBS")); jobsStr != "" {
			if jobs, err := strconv.Atoi(jobsStr); err == nil && jobs > 0 {
				envLines = append(envLines, fmt.Sprintf("export PEACOCK_JOBS=%d", jobs))
			}
		}
		envScriptPath := filepath.Join(buildDir, "peacock-env.sh")
		envContent := "#!/bin/sh\n" + strings.Join(envLines, "\n") + "\n"
		if err := os.WriteFile(envScriptPath, []byte(envContent), 0644); err != nil {
			return "", err
		}
		cmdArgs := []string{"/bin/sh", "-c", fmt.Sprintf("cd %s && . ./peacock-env.sh && /bin/sh ./peacock-build.sh", buildDirInChroot)}

		if masterRoot != "" {
			// Nested execution: Host -> Master -> Target -> Command

			// 1. Mount Target into Master
			mountPoint := filepath.Join(masterRoot, "mnt", "build-execution")
			if err := runner.RunCmd(exec.Command("sudo", "mkdir", "-p", mountPoint)); err != nil {
				return "", err
			}
			if err := runner.RunCmd(exec.Command("sudo", "mount", "--rbind", root, mountPoint)); err != nil {
				return "", fmt.Errorf("failed to mount target for execution: %w", err)
			}
			defer runner.RunCmd(exec.Command("sudo", "umount", mountPoint))

			// 2. Construct nested command
			// chroot master chroot /mnt/build-execution ...

			// We need to be careful with quoting. cmdArgs[0] is /bin/sh, [1] is -c, [2] is script
			// cmdArgs is actually: /bin/sh -c "cd ... && ..."

			// Target chroot cmd: chroot /mnt/build-execution /bin/sh -c "..."
			// Master chroot cmd: chroot masterRoot chroot /mnt/build-execution /bin/sh -c "..."

			// We construct the "inner" chroot command as a string to pass to master shell?
			// Or just chain args?
			// sudo chroot masterRoot chroot /mnt/build-execution /bin/sh -c "..." works if passed as args to exec.

			nestedArgs := []string{"chroot", masterRoot, "chroot", "/mnt/build-execution"}
			nestedArgs = append(nestedArgs, cmdArgs...)

			nestedCmd := exec.Command("sudo", nestedArgs...)
			nestedCmd.Stdout = runner.LogWriter()
			nestedCmd.Stderr = runner.LogWriter()

			if err := runner.RunCmd(nestedCmd); err != nil {
				return "", fmt.Errorf("nested build script failed: %w", err)
			}
		} else {
			if err := chroot.EnterWithSudo(root, cmdArgs); err != nil {
				return "", fmt.Errorf("build script failed: %w", err)
			}
		}
	}

	// Return build dir on host filesystem
	return buildDir, nil
}
