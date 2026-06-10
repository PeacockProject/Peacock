package main

// cobra `peacock build` command. Thin wrapper around
// internal/pipeline.Runner — the actual orchestration (5 phases, all
// the chroot helpers, image assembly) lives in internal/pipeline so the
// peacock-builder Wails GUI can call into it in-process instead of
// subprocess-execing the CLI.
//
// What stays here:
//
//   - Cobra flag wiring + viper binding.
//   - The interactive prompt loop for missing flags (--user, --desktop, etc).
//     Pipeline package never prompts; the GUI / automation callers supply
//     complete configs.
//   - The signal-handler-driven mount cleanup (uses pipeline.UnmountPeacockMounts
//     + a pipeline.Cleanup as the belt-and-braces). The Run() inside the
//     pipeline is the primary cleanup; the outer one fires only if the
//     pipeline is cancelled mid-phase.
//   - The build log file setup (pipeline reads runner.LogWriter — we set it
//     so subprocesses log to the file).

import (
	"bufio"
	"context"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"peacock/internal/config"
	"peacock/internal/host"
	"peacock/internal/pipeline"
	"peacock/internal/runner"
	"peacock/internal/userland"
	"peacock/pkg/buildconfig"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

// cobra-bound flag mirrors. Phase A of the pipeline-lift moved the
// orchestration out; these vars remain here purely as pflag targets,
// folded into a pipeline.RunnerOpts before invocation.
var (
	buildDeviceName       string
	buildUseQemuFlag      string
	buildCrossCompileFlag string
	buildEmptyRootfsFlag  bool
)

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build the distribution image",
	Long: `Build the distribution image for the selected device.
This process involves:
1. Creating a blank image file.
2. Partitioning and formatting.
3. Installing the base system and device packages.
4. Installing the bootloader.`,
	Run: func(cmd *cobra.Command, args []string) {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		runner.SetContext(ctx)

		sigCh := make(chan os.Signal, 1)
		signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
		go func() {
			<-sigCh
			fmt.Println("\nInterrupt received, stopping...")

			// Best-effort cleanup of peacock-owned mountpoints only.
			workDir := config.WorkDir()
			if workDir != "" {
				fmt.Println("Cleaning up mounts...")
				if err := pipeline.UnmountPeacockMounts(workDir); err != nil {
					fmt.Fprintf(os.Stderr, "warning: mount cleanup failed: %v\n", err)
				}
				cmd := exec.Command("sudo", "find", workDir, "-name", "db.lck", "-delete")
				cmd.Stdout = os.Stdout
				cmd.Stderr = os.Stderr
				_ = cmd.Run()
			}

			cancel()
		}()

		workDir := config.WorkDir()
		if workDir == "" {
			fmt.Println("Work directory not set. Please run 'peacock init' first.")
			os.Exit(1)
		}
		cleanup := pipeline.NewCleanup(workDir)
		defer cleanup.Run()
		fatal := func() {
			cleanup.Run()
			os.Exit(1)
		}

		logDir := filepath.Join(workDir, "logs")
		if err := os.MkdirAll(logDir, 0755); err == nil {
			logPath := filepath.Join(logDir, fmt.Sprintf("build-%s-%s.log", buildDeviceName, time.Now().Format("20060102-150405")))
			if f, err := os.Create(logPath); err == nil {
				defer f.Close()
				runner.SetLogWriter(f)
				fmt.Printf("Build log: %s\n", logPath)
			}
		}

		// Run the interactive prompts the GUI doesn't need (TTY-only).
		// The pipeline package treats viper as the source of truth for
		// these, so we push the resolved values back into viper before
		// invoking Runner.Run.
		populateInteractivePrompts()

		cfg := buildconfig.BuildPipelineConfig{
			Device:         buildDeviceName,
			Flavor:         config.Flavor(),
			InitSystem:     config.InitSystem(),
			Desktop:        config.Desktop(),
			DisplayManager: config.DisplayManager(),
			Extras:         config.ExtraPackages(),
			UserName:       config.UserName(),
			UserPassword:   config.UserPassword(),
			ImageSizeMB:    config.ImageSizeMB(),
			EmptyRootfs:    config.EmptyRootfs(),
			UseQemu:        buildUseQemuFlag,
			CrossCompile:   buildCrossCompileFlag,
			WorkDir:        workDir,
		}

		// Resolve --use-host-chroot (or PEACOCK_HOST_CHROOT) and validate
		// the flavor early, before any host work, so an invalid flavor is
		// a clean fast error rather than a mid-build surprise. Empty =
		// host-chroot mode off; the build shells out directly as today.
		hostChroot := hostChrootFlavor()
		if hostChroot != "" && !host.IsSupportedHostChrootFlavor(hostChroot) {
			fmt.Printf("--use-host-chroot=%s: unsupported flavor (supported: %v)\n", hostChroot, host.SupportedHostChrootFlavors)
			fatal()
		}

		runnerOpts := pipeline.RunnerOpts{
			Device:           buildDeviceName,
			UseQemu:          buildUseQemuFlag,
			CrossCompile:     buildCrossCompileFlag,
			EmptyRootfs:      buildEmptyRootfsFlag,
			HostChrootFlavor: hostChroot,
		}

		imagePath, err := pipeline.NewRunner(runnerOpts).Run(ctx, cfg)
		if err != nil {
			fmt.Printf("%v\n", err)
			fatal()
		}

		fmt.Println("Build complete! Image at: " + imagePath)
	},
}

// populateInteractivePrompts replicates the TTY prompts that the
// pre-pipeline-lift runBuildSetup used to perform. They're cobra-only
// here — the GUI / automation callers supply complete configs via
// buildconfig.BuildPipelineConfig and skip this code path entirely.
func populateInteractivePrompts() {
	if buildEmptyRootfsFlag {
		// Empty-rootfs mode skips all the user/desktop/extras prompts.
		return
	}
	reader := bufio.NewReader(os.Stdin)
	if len(config.ExtraPackages()) == 0 {
		extras := promptCSV(reader, "Extra packages (comma-separated, empty for none)")
		if extras != nil {
			viper.Set(config.KeyExtraPackages, extras)
		}
	}
	if config.Desktop() == "" {
		fmt.Print(userland.DescribeChoices())
		viper.Set(config.KeyDesktop, promptSelect(reader, "Desktop", userland.DesktopNames(), "none"))
	}
	if config.DisplayManager() == "" {
		viper.Set(config.KeyDisplayManager, promptSelect(reader, "Display manager", userland.DisplayManagerNames(), "none"))
	}
	if config.UserName() == "" {
		viper.Set(config.KeyUserName, promptLine(reader, "Username (empty to skip user creation)", ""))
	}
	if config.UserName() != "" && config.UserPassword() == "" {
		viper.Set(config.KeyUserPassword, promptPassword(reader, "Password (plaintext)", "Confirm password"))
	}
}

func init() {
	rootCmd.AddCommand(buildCmd)
	buildCmd.Flags().StringVar(&buildDeviceName, "device", "", "Device codename (e.g. samsung-i9500)")
	buildCmd.Flags().String("init", "systemd", "Init system (systemd, openrc)")
	buildCmd.Flags().String("desktop", "", "Desktop environment (none, xfce, lxqt, mate, gnome, plasma, cinnamon)")
	buildCmd.Flags().String("display-manager", "", "Display manager (none, lightdm, greetd, sddm, gdm, ly)")
	buildCmd.Flags().StringSlice("extra", nil, "Extra packages to include in rootfs")
	buildCmd.Flags().String("user", "", "Create user account in rootfs")
	buildCmd.Flags().String("password", "", "Password for --user (plaintext)")
	buildCmd.Flags().Int("image-size", 0, "Disk image size in MB (0 = auto)")
	buildCmd.Flags().BoolVar(&buildEmptyRootfsFlag, "empty-rootfs", false, "Create a small debug image with boot assets only and an empty labeled root partition")
	buildCmd.Flags().StringVar(&buildUseQemuFlag, "use-qemu", "auto", "Use qemu for foreign arch builds: auto|true|false")
	buildCmd.Flags().StringVar(&buildCrossCompileFlag, "cross-compile", "", "Cross compiler prefix (e.g. arm-none-eabi-)")
	buildCmd.Flags().String("flavor", "arch", "Base-distro flavor: arch|debian|alpine")
	viper.BindPFlag(config.KeyFlavor, buildCmd.Flags().Lookup("flavor"))
	viper.BindPFlag(config.KeyInitSystem, buildCmd.Flags().Lookup("init"))
	viper.BindPFlag(config.KeyDesktop, buildCmd.Flags().Lookup("desktop"))
	viper.BindPFlag(config.KeyDisplayManager, buildCmd.Flags().Lookup("display-manager"))
	viper.BindPFlag(config.KeyExtraPackages, buildCmd.Flags().Lookup("extra"))
	viper.BindPFlag(config.KeyUserName, buildCmd.Flags().Lookup("user"))
	viper.BindPFlag(config.KeyUserPassword, buildCmd.Flags().Lookup("password"))
	viper.BindPFlag(config.KeyImageSizeMB, buildCmd.Flags().Lookup("image-size"))
	viper.BindPFlag(config.KeyEmptyRootfs, buildCmd.Flags().Lookup("empty-rootfs"))
	buildCmd.MarkFlagRequired("device")
}

// promptLine reads a single line from r with an optional default value.
// Kept here (rather than in pipeline) because it's a TTY-only helper:
// the pipeline package never blocks on stdin.
func promptLine(r *bufio.Reader, label, def string) string {
	if def != "" {
		fmt.Printf("%s [%s]: ", label, def)
	} else {
		fmt.Printf("%s: ", label)
	}
	line, _ := r.ReadString('\n')
	line = strings.TrimSpace(line)
	if line == "" {
		return def
	}
	return line
}

func promptCSV(r *bufio.Reader, label string) []string {
	line := promptLine(r, label, "")
	if line == "" {
		return nil
	}
	parts := strings.Split(line, ",")
	out := make([]string, 0, len(parts))
	for _, p := range parts {
		p = strings.TrimSpace(p)
		if p != "" {
			out = append(out, p)
		}
	}
	return out
}

func promptSelect(r *bufio.Reader, label string, options []string, def string) string {
	fmt.Printf("%s options: %s\n", label, strings.Join(options, ", "))
	for {
		v := promptLine(r, label, def)
		for _, o := range options {
			if v == o {
				return v
			}
		}
		fmt.Printf("Invalid %s: %s\n", label, v)
	}
}

func promptPassword(r *bufio.Reader, label, confirmLabel string) string {
	for {
		pw := promptLine(r, label, "")
		confirm := promptLine(r, confirmLabel, "")
		if pw == confirm {
			return pw
		}
		fmt.Println("Passwords do not match, try again.")
	}
}
