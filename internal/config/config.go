package config

import (
	"encoding/json"
	"os"
	"path/filepath"

	"github.com/spf13/viper"
)

// Viper key names. These match the JSON keys persisted in config.json and
// must not change without a migration, or existing user configs will break.
const (
	KeyWorkDir        = "work_dir"
	KeyInitSystem     = "init_system"
	KeyDesktop        = "desktop"
	KeyDisplayManager = "display_manager"
	KeyExtraPackages  = "extra_packages"
	KeyUserName       = "user_name"
	KeyUserPassword   = "user_password"
	KeyEmptyRootfs    = "empty_rootfs"
	KeyImageSizeMB    = "image_size_mb"
)

// Typed accessors. Each accessor returns the underlying viper value with the
// correct type; callers should treat the empty/zero value as meaningful (e.g.
// WorkDir() == "" indicates the user has not run `peacock init`).

// WorkDir returns the configured work directory, or "" if unset.
func WorkDir() string { return viper.GetString(KeyWorkDir) }

// InitSystem returns the selected init system (e.g. "systemd", "openrc"), or "" if unset.
func InitSystem() string { return viper.GetString(KeyInitSystem) }

// Desktop returns the selected desktop environment name, or "" if unset.
func Desktop() string { return viper.GetString(KeyDesktop) }

// DisplayManager returns the selected display manager name, or "" if unset.
func DisplayManager() string { return viper.GetString(KeyDisplayManager) }

// ExtraPackages returns the list of additional packages to install.
func ExtraPackages() []string { return viper.GetStringSlice(KeyExtraPackages) }

// UserName returns the username to create in the rootfs, or "" to skip.
func UserName() string { return viper.GetString(KeyUserName) }

// UserPassword returns the plaintext password for the user, or "" if unset.
func UserPassword() string { return viper.GetString(KeyUserPassword) }

// EmptyRootfs returns whether the build should produce a minimal debug image.
func EmptyRootfs() bool { return viper.GetBool(KeyEmptyRootfs) }

// ImageSizeMB returns the requested disk image size in megabytes, or 0 for auto.
func ImageSizeMB() int { return viper.GetInt(KeyImageSizeMB) }

// Config holds the application configuration
type Config struct {
	WorkDir string `json:"work_dir"`
}

// SaveConfig writes the configuration to the specified path
func SaveConfig(cfg *Config, path string) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(path, data, 0644)
}
