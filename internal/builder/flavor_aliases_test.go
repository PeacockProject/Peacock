package builder

import (
	"bytes"
	"reflect"
	"strings"
	"testing"

	"peacock/internal/runner"
)

// captureRunnerLog swaps the runner log writer for a buffer and
// returns a func the caller defers to restore it. Used to assert on
// the warning text the alias resolver emits when a flavor table is
// missing or unparsable.
func captureRunnerLog(t *testing.T) (*bytes.Buffer, func()) {
	t.Helper()
	buf := &bytes.Buffer{}
	old := runner.LogWriter()
	runner.SetLogWriter(buf)
	return buf, func() { runner.SetLogWriter(old) }
}

// setAliasesRoot points the resolver at a hermetic testdata dir and
// resets the alias cache so previous sub-tests don't bleed through.
func setAliasesRoot(t *testing.T, root string) {
	t.Helper()
	old := FlavorAliasesRoot
	FlavorAliasesRoot = root
	ResetAliasCache()
	t.Cleanup(func() {
		FlavorAliasesRoot = old
		ResetAliasCache()
	})
}

func TestResolveBuildDeps_ArchIdentity(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	in := []string{"gcc", "base-devel"}
	got := ResolveBuildDeps(in, "arch")
	want := []string{"gcc", "base-devel"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("identity mapping: got %v want %v", got, want)
	}
	// Mutating the input slice afterwards must not affect the output —
	// the resolver returns a fresh slice.
	in[0] = "MUTATED"
	if got[0] == "MUTATED" {
		t.Fatalf("ResolveBuildDeps did not return a fresh slice (aliased the input)")
	}
}

func TestResolveBuildDeps_DebianNonIdentity(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	got := ResolveBuildDeps([]string{"base-devel", "ninja"}, "debian")
	want := []string{"build-essential", "ninja-build"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("debian rewrite: got %v want %v", got, want)
	}
}

func TestResolveBuildDeps_MissingFileWarns(t *testing.T) {
	// Point at an empty root so neither arch nor "made-up" have a
	// file. We use the "made-up" flavor to ensure the missing-file
	// warning path is hit even on hosts where the real peacock-ports
	// tree is present.
	setAliasesRoot(t, "testdata/aliases-good")
	buf, restore := captureRunnerLog(t)
	defer restore()
	in := []string{"gcc", "make"}
	got := ResolveBuildDeps(in, "no-such-flavor")
	if !reflect.DeepEqual(got, in) {
		t.Fatalf("missing alias file should be identity: got %v want %v", got, in)
	}
	if !strings.Contains(buf.String(), "warning") {
		t.Fatalf("expected warning on missing alias file, log was: %q", buf.String())
	}
	if !strings.Contains(buf.String(), "no-such-flavor") {
		t.Fatalf("expected warning to mention flavor name, log was: %q", buf.String())
	}
}

func TestResolveBuildDeps_EmptyAliasMap(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	buf, restore := captureRunnerLog(t)
	defer restore()
	in := []string{"gcc", "make"}
	got := ResolveBuildDeps(in, "empty")
	if !reflect.DeepEqual(got, in) {
		t.Fatalf("empty alias table should be identity: got %v want %v", got, in)
	}
	// Present-but-empty must NOT log a missing-file warning.
	if strings.Contains(buf.String(), "warning") {
		t.Fatalf("present-but-empty alias file should not warn, log was: %q", buf.String())
	}
}

func TestResolveBuildDeps_MultiTargetExpansion(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	got := ResolveBuildDeps([]string{"util-linux"}, "debian")
	want := []string{"util-linux", "libblkid-dev", "libmount-dev", "libuuid1-dev"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("util-linux should expand to 4 entries: got %v want %v", got, want)
	}
}

func TestResolveBuildDeps_MultiTargetMixedWithPlain(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	got := ResolveBuildDeps([]string{"base-devel", "util-linux", "ninja"}, "debian")
	want := []string{
		"build-essential",
		"util-linux", "libblkid-dev", "libmount-dev", "libuuid1-dev",
		"ninja-build",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("mixed plain + multi-target: got %v want %v", got, want)
	}
}

func TestResolveBuildDeps_UnknownFlavorWarns(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	buf, restore := captureRunnerLog(t)
	defer restore()
	in := []string{"gcc"}
	got := ResolveBuildDeps(in, "fedora")
	if !reflect.DeepEqual(got, in) {
		t.Fatalf("unknown flavor should be identity: got %v want %v", got, in)
	}
	if !strings.Contains(buf.String(), "warning") {
		t.Fatalf("expected warning on unknown flavor, log was: %q", buf.String())
	}
}

func TestResolveBuildDeps_EmptyInputIsNoOp(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	got := ResolveBuildDeps(nil, "debian")
	if len(got) != 0 {
		t.Fatalf("empty input should produce empty output, got %v", got)
	}
}

func TestResolveBuildDeps_EmptyFlavorDefaultsToArch(t *testing.T) {
	setAliasesRoot(t, "testdata/aliases-good")
	// "" should be treated as "arch", which has only identity entries,
	// so the output equals the input.
	in := []string{"gcc"}
	got := ResolveBuildDeps(in, "")
	if !reflect.DeepEqual(got, in) {
		t.Fatalf("empty flavor should default to arch identity: got %v want %v", got, in)
	}
}
