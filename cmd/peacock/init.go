package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"peacock/internal/config"

	"github.com/spf13/cobra"
)

// initCmd represents the init command
var initCmd = &cobra.Command{
	Use:   "init",
	Short: "Initialize the Peacock workspace",
	Long: `Initialize the Peacock workspace.
This will ask for a working directory and create the initial configuration.`,
	Run: func(cmd *cobra.Command, args []string) {
		reader := bufio.NewReader(os.Stdin)

		defaultWorkDir := filepath.Join(os.Getenv("HOME"), ".local", "var", "peacock")
		fmt.Printf("Work directory [%s]: ", defaultWorkDir)
		workDir, _ := reader.ReadString('\n')
		workDir = strings.TrimSpace(workDir)
		if workDir == "" {
			workDir = defaultWorkDir
		}

		workDir, err := filepath.Abs(workDir)
		if err != nil {
			fmt.Printf("Error resolving path: %v\n", err)
			os.Exit(1)
		}

		// Save to config
		cfg := &config.Config{
			WorkDir: workDir,
		}

		home, _ := os.UserHomeDir()
		configPath := filepath.Join(home, ".config", "peacock", "config.json")
		if cfgFile != "" {
			configPath = cfgFile
		}

		if err := config.SaveConfig(cfg, configPath); err != nil {
			fmt.Printf("Error saving config: %v\n", err)
			os.Exit(1)
		}

		fmt.Printf("Configuration saved to %s\n", configPath)
		fmt.Printf("Work directory set to: %s\n", workDir)

		// Create the work directory
		if err := os.MkdirAll(workDir, 0755); err != nil {
			fmt.Printf("Error creating work directory: %v\n", err)
			os.Exit(1)
		}
	},
}

func init() {
	rootCmd.AddCommand(initCmd)
}
