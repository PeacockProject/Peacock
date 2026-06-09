package buildconfig

import (
	"strings"
	"testing"
)

// goodConfig returns a minimal config that passes Validate. Each table
// case below mutates one field to exercise a single rejection path.
func goodConfig() BuildPipelineConfig {
	return BuildPipelineConfig{
		Device:  "oppo-a16",
		Flavor:  "arch",
		WorkDir: "/tmp/peacock-work",
	}
}

func TestValidate(t *testing.T) {
	cases := []struct {
		name    string
		mutate  func(*BuildPipelineConfig)
		wantErr string // substring; empty means Validate must pass
	}{
		{name: "happy path", mutate: func(c *BuildPipelineConfig) {}},
		{
			// Flavor is optional at this layer; the pipeline defaults
			// empty to "arch" when pushing config into viper.
			name:   "empty flavor allowed",
			mutate: func(c *BuildPipelineConfig) { c.Flavor = "" },
		},
		{
			name:   "debian flavor allowed",
			mutate: func(c *BuildPipelineConfig) { c.Flavor = "debian" },
		},
		{
			name:   "alpine flavor allowed",
			mutate: func(c *BuildPipelineConfig) { c.Flavor = "alpine" },
		},
		{
			// Validate only checks required fields; everything optional
			// stays optional even when populated.
			name: "full optional fields allowed",
			mutate: func(c *BuildPipelineConfig) {
				c.InitSystem = "openrc"
				c.Desktop = "xfce"
				c.DisplayManager = "lightdm"
				c.Extras = []string{"htop", "vim"}
				c.UserName = "peacock"
				c.UserPassword = "hunter2"
				c.ImageSizeMB = 4096
				c.EmptyRootfs = true
				c.UseQemu = "false"
				c.CrossCompile = "arm-none-eabi-"
				c.Architecture = "aarch64"
			},
		},
		{
			name:    "missing device",
			mutate:  func(c *BuildPipelineConfig) { c.Device = "" },
			wantErr: "Device is required",
		},
		{
			name:    "missing workdir",
			mutate:  func(c *BuildPipelineConfig) { c.WorkDir = "" },
			wantErr: "WorkDir is required",
		},
		{
			name:    "unknown flavor",
			mutate:  func(c *BuildPipelineConfig) { c.Flavor = "gentoo" },
			wantErr: "invalid Flavor",
		},
		{
			// Flavor matching is exact, not case-folded.
			name:    "flavor is case sensitive",
			mutate:  func(c *BuildPipelineConfig) { c.Flavor = "Arch" },
			wantErr: "invalid Flavor",
		},
		{
			// Device is checked before WorkDir; pin the precedence so
			// error-driven UIs surface the first missing field stably.
			name: "device checked before workdir",
			mutate: func(c *BuildPipelineConfig) {
				c.Device = ""
				c.WorkDir = ""
			},
			wantErr: "Device is required",
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			cfg := goodConfig()
			tc.mutate(&cfg)
			err := cfg.Validate()
			if tc.wantErr == "" {
				if err != nil {
					t.Fatalf("Validate() = %v, want nil", err)
				}
				return
			}
			if err == nil {
				t.Fatalf("Validate() = nil, want error containing %q", tc.wantErr)
			}
			if !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("Validate() = %q, want substring %q", err, tc.wantErr)
			}
		})
	}
}

// TestValidateNilReceiver pins the nil-receiver guard: a forgotten
// pointer must produce a diagnosable error, not a panic.
func TestValidateNilReceiver(t *testing.T) {
	var cfg *BuildPipelineConfig
	err := cfg.Validate()
	if err == nil {
		t.Fatal("Validate() on nil receiver = nil, want error")
	}
	if !strings.Contains(err.Error(), "nil config") {
		t.Fatalf("Validate() = %q, want substring %q", err, "nil config")
	}
}
