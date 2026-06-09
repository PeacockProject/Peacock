package pipeline

import (
	"context"
	"errors"
	"reflect"
	"testing"
	"time"

	"github.com/spf13/viper"

	"peacock/internal/config"
	"peacock/pkg/buildconfig"
)

// resetViper clears viper now and after the test so config state never
// bleeds between subtests (or into other packages' tests).
func resetViper(t *testing.T) {
	t.Helper()
	viper.Reset()
	t.Cleanup(viper.Reset)
}

func TestNewRunnerRetainsOpts(t *testing.T) {
	cases := []struct {
		name string
		in   RunnerOpts
		want RunnerOpts
	}{
		{
			name: "full opts round-trip",
			in: RunnerOpts{
				Device:       "oppo-a16",
				UseQemu:      "false",
				CrossCompile: "arm-none-eabi-",
				EmptyRootfs:  true,
			},
			want: RunnerOpts{
				Device:       "oppo-a16",
				UseQemu:      "false",
				CrossCompile: "arm-none-eabi-",
				EmptyRootfs:  true,
			},
		},
		{
			name: "empty UseQemu defaults to auto",
			in:   RunnerOpts{Device: "oppo-a16"},
			want: RunnerOpts{Device: "oppo-a16", UseQemu: "auto"},
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := NewRunner(tc.in).Opts()
			if got != tc.want {
				t.Fatalf("Opts() = %+v, want %+v", got, tc.want)
			}
		})
	}
}

func TestPushConfig(t *testing.T) {
	t.Run("full config reaches accessors", func(t *testing.T) {
		resetViper(t)
		cfg := buildconfig.BuildPipelineConfig{
			Device:         "oppo-a16",
			Flavor:         "alpine",
			InitSystem:     "openrc",
			Desktop:        "xfce",
			DisplayManager: "lightdm",
			Extras:         []string{"htop", "vim"},
			UserName:       "peacock",
			UserPassword:   "hunter2",
			ImageSizeMB:    4096,
			EmptyRootfs:    true,
			WorkDir:        "/tmp/peacock-work",
		}
		pushConfig(cfg)

		if got := config.Flavor(); got != "alpine" {
			t.Errorf("Flavor() = %q, want %q", got, "alpine")
		}
		if got := config.InitSystem(); got != "openrc" {
			t.Errorf("InitSystem() = %q, want %q", got, "openrc")
		}
		if got := config.Desktop(); got != "xfce" {
			t.Errorf("Desktop() = %q, want %q", got, "xfce")
		}
		if got := config.DisplayManager(); got != "lightdm" {
			t.Errorf("DisplayManager() = %q, want %q", got, "lightdm")
		}
		if got := config.ExtraPackages(); !reflect.DeepEqual(got, []string{"htop", "vim"}) {
			t.Errorf("ExtraPackages() = %v, want %v", got, []string{"htop", "vim"})
		}
		if got := config.UserName(); got != "peacock" {
			t.Errorf("UserName() = %q, want %q", got, "peacock")
		}
		if got := config.UserPassword(); got != "hunter2" {
			t.Errorf("UserPassword() = %q, want %q", got, "hunter2")
		}
		if got := config.ImageSizeMB(); got != 4096 {
			t.Errorf("ImageSizeMB() = %d, want %d", got, 4096)
		}
		if got := config.EmptyRootfs(); !got {
			t.Errorf("EmptyRootfs() = false, want true")
		}
		if got := config.WorkDir(); got != "/tmp/peacock-work" {
			t.Errorf("WorkDir() = %q, want %q", got, "/tmp/peacock-work")
		}
	})

	t.Run("empty flavor defaults to arch", func(t *testing.T) {
		resetViper(t)
		pushConfig(buildconfig.BuildPipelineConfig{Device: "d", WorkDir: "/w"})
		if got := viper.GetString(config.KeyFlavor); got != "arch" {
			t.Fatalf("flavor key = %q, want %q", got, "arch")
		}
	})

	t.Run("empty workdir does not clobber persisted key", func(t *testing.T) {
		resetViper(t)
		viper.Set(config.KeyWorkDir, "/persisted")
		pushConfig(buildconfig.BuildPipelineConfig{Device: "d"})
		if got := config.WorkDir(); got != "/persisted" {
			t.Fatalf("WorkDir() = %q, want %q", got, "/persisted")
		}
	})
}

func TestRunRejectsInvalidConfigBeforePhases(t *testing.T) {
	resetViper(t)
	r := NewRunner(RunnerOpts{Device: "oppo-a16"})
	r.setupFn = func(ctx context.Context, workDir string) (*buildSetup, error) {
		t.Fatal("phase 1 ran despite invalid config")
		return nil, nil
	}
	// Missing WorkDir must fail validation before any phase runs.
	_, err := r.Run(context.Background(), buildconfig.BuildPipelineConfig{Device: "oppo-a16"})
	if err == nil {
		t.Fatal("Run() = nil error, want validation error")
	}
}

func TestRunMergesConfigIntoOpts(t *testing.T) {
	resetViper(t)
	sentinel := errors.New("stop after phase 1")
	r := NewRunner(RunnerOpts{})
	r.setupFn = func(ctx context.Context, workDir string) (*buildSetup, error) {
		return nil, sentinel
	}

	cfg := buildconfig.BuildPipelineConfig{
		Device:       "oppo-a16",
		WorkDir:      t.TempDir(),
		UseQemu:      "false",
		CrossCompile: "arm-none-eabi-",
		EmptyRootfs:  true,
	}
	_, err := r.Run(context.Background(), cfg)
	if !errors.Is(err, sentinel) {
		t.Fatalf("Run() error = %v, want wrapped sentinel", err)
	}

	got := r.Opts()
	want := RunnerOpts{
		Device:       "oppo-a16",
		UseQemu:      "false",
		CrossCompile: "arm-none-eabi-",
		EmptyRootfs:  true,
	}
	if got != want {
		t.Fatalf("Opts() after Run = %+v, want %+v", got, want)
	}
}

func TestRunHonorsContextCancellation(t *testing.T) {
	resetViper(t)
	ctx, cancel := context.WithCancel(context.Background())

	entered := make(chan struct{})
	r := NewRunner(RunnerOpts{Device: "oppo-a16"})
	r.setupFn = func(ctx context.Context, workDir string) (*buildSetup, error) {
		close(entered)
		<-ctx.Done() // a well-behaved phase blocks until cancellation
		return nil, ctx.Err()
	}

	cfg := buildconfig.BuildPipelineConfig{Device: "oppo-a16", WorkDir: t.TempDir()}
	result := make(chan error, 1)
	go func() {
		_, err := r.Run(ctx, cfg)
		result <- err
	}()

	select {
	case <-entered:
	case <-time.After(5 * time.Second):
		t.Fatal("phase 1 never started")
	}
	cancel()

	select {
	case err := <-result:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("Run() error = %v, want context.Canceled", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("Run() did not return promptly after cancellation")
	}
}
