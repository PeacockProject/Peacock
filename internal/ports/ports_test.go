package ports

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

// mkPortsTree creates dir/device so it passes the hasDevice sanity guard.
func mkPortsTree(t *testing.T, dir string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Join(dir, "device"), 0755); err != nil {
		t.Fatalf("mkPortsTree(%s): %v", dir, err)
	}
}

func TestResolve_EnvWithDevice(t *testing.T) {
	dir := t.TempDir()
	mkPortsTree(t, dir)
	t.Setenv("PEACOCK_PORTS_DIR", dir)

	got, found := Resolve()
	if !found {
		t.Fatalf("Resolve() found=false, want true")
	}
	if got != dir {
		t.Fatalf("Resolve() = %q, want %q", got, dir)
	}
}

func TestResolve_EnvWithoutDevice(t *testing.T) {
	dir := t.TempDir() // no device/ subdir
	t.Setenv("PEACOCK_PORTS_DIR", dir)
	// Run from a temp cwd with no ./peacock-ports so we don't pick up
	// the dev-layout symlink and report a false negative.
	t.Chdir(t.TempDir())
	// Redirect HOME to an empty dir so the <varDir>/peacock-ports
	// candidate (#3) can't resolve to a real checkout that happens to
	// exist on the test machine (~/.local/var/peacock/peacock-ports).
	t.Setenv("HOME", t.TempDir())

	if _, found := Resolve(); found {
		t.Fatalf("Resolve() found=true for a dir without device/, want false")
	}
}

func TestResolve_EnvBeatsCwd(t *testing.T) {
	// env candidate
	envDir := t.TempDir()
	mkPortsTree(t, envDir)
	t.Setenv("PEACOCK_PORTS_DIR", envDir)

	// cwd candidate: ./peacock-ports also valid
	cwd := t.TempDir()
	mkPortsTree(t, filepath.Join(cwd, "peacock-ports"))
	t.Chdir(cwd)

	got, found := Resolve()
	if !found {
		t.Fatalf("Resolve() found=false, want true")
	}
	if got != envDir {
		t.Fatalf("Resolve() = %q, want env dir %q (env must beat cwd)", got, envDir)
	}
}

func TestCloneArgs(t *testing.T) {
	got := cloneArgs("https://example.invalid/x", "/tmp/dest")
	want := []string{"clone", "--depth", "1", "https://example.invalid/x", "/tmp/dest"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("cloneArgs() = %v, want %v", got, want)
	}
}

func TestCloneURL_Default(t *testing.T) {
	t.Setenv("PEACOCK_PORTS_URL", "")
	if got := cloneURL(); got != DefaultURL {
		t.Fatalf("cloneURL() = %q, want default %q", got, DefaultURL)
	}
}

func TestCloneURL_EnvOverride(t *testing.T) {
	const ssh = "git@github.com:PeacockProject/peacock-ports.git"
	t.Setenv("PEACOCK_PORTS_URL", ssh)
	if got := cloneURL(); got != ssh {
		t.Fatalf("cloneURL() = %q, want override %q", got, ssh)
	}
}

// Ensure()'s clone path requires network + git and is left to
// integration testing. The unit tests above cover the pure pieces
// (Resolve precedence, cloneArgs vector, cloneURL selection).
