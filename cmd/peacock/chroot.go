package main

import (
	"fmt"
	"os"
	"path/filepath"

	"peacock/internal/chroot"
	"peacock/internal/config"

	"github.com/spf13/cobra"
)

// chrootCmd represents the chroot command
var chrootCmd = &cobra.Command{
	Use:   "chroot",
	Short: "Enter the chroot environment",
	Long: `Enter the chroot environment.
This will mount necessary filesystems and drop you into a shell inside the work directory's chroot.`,
	Run: func(cmd *cobra.Command, args []string) {
		workDir := config.WorkDir()
		if workDir == "" {
			fmt.Println("Work directory not set. Please run 'peacock init' first.")
			os.Exit(1)
		}

		// Assume default chroot location for now: workDir/chroot
		// In reality we might have separate chroots for architectures.
		// For this step, let's assume a "native" chroot or just "chroot".
		target := filepath.Join(workDir, "chroot")

		if _, err := os.Stat(target); os.IsNotExist(err) {
			fmt.Printf("Chroot directory %s does not exist.\n", target)
			os.Exit(1)
		}

		fmt.Printf("Mounting special filesystems in %s...\n", target)
		if err := chroot.Mount(target); err != nil {
			fmt.Printf("Error mounting: %v\n", err)
			os.Exit(1)
		}

		// Ensure unmount on exit
		defer func() {
			fmt.Println("Unmounting...")
			chroot.Unmount(target)
		}()

		fmt.Println("Entering chroot...")
		if err := chroot.Enter(target, args); err != nil {
			fmt.Printf("Error running in chroot: %v\n", err)
			// Do not exit with 1 here immediately, let defer unmount happen.
		}
	},
}

func init() {
	rootCmd.AddCommand(chrootCmd)
}
