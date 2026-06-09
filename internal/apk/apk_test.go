package apk

import (
	"os"
	"strings"
	"testing"
)

func TestArchToApk(t *testing.T) {
	t.Parallel()
	cases := []struct {
		in   string
		want string
	}{
		{"aarch64", "aarch64"},
		{"armv7", "armv7"},
		{"armv7h", "armv7"},
		{"x86_64", "x86_64"},
		// Unknown architectures fall through to "" so callers can
		// produce a clear error at the call site.
		{"", ""},
		{"riscv64", ""},
		{"mips", ""},
		{"powerpc", ""},
	}
	for _, c := range cases {
		c := c
		t.Run(c.in, func(t *testing.T) {
			t.Parallel()
			got := archToApk(c.in)
			if got != c.want {
				t.Fatalf("archToApk(%q) = %q, want %q", c.in, got, c.want)
			}
		})
	}
}

func TestGenerateConfigContentDefaults(t *testing.T) {
	// Golden compare for the v3.20 / main+community shape Config picks
	// up when callers don't override Version / Mirror / Branches.
	// Update this string in lock step with DefaultVersion /
	// DefaultBranches changes.
	cfg := Config{Arch: "aarch64"}
	got := GenerateConfigContent(cfg)
	want := "https://dl-cdn.alpinelinux.org/alpine/v3.20/main\n" +
		"https://dl-cdn.alpinelinux.org/alpine/v3.20/community\n"
	if got != want {
		t.Fatalf("GenerateConfigContent default mismatch\n got: %q\nwant: %q", got, want)
	}
}

func TestGenerateConfigContentExplicit(t *testing.T) {
	cfg := Config{
		Version:  V3_19,
		Arch:     "x86_64",
		Mirror:   "https://dl-3.alpinelinux.org/alpine",
		Branches: []string{"main", "community", "testing"},
	}
	got := GenerateConfigContent(cfg)
	wantLines := []string{
		"https://dl-3.alpinelinux.org/alpine/v3.19/main",
		"https://dl-3.alpinelinux.org/alpine/v3.19/community",
		"https://dl-3.alpinelinux.org/alpine/v3.19/testing",
	}
	want := strings.Join(wantLines, "\n") + "\n"
	if got != want {
		t.Fatalf("GenerateConfigContent explicit mismatch\n got: %q\nwant: %q", got, want)
	}
}

func TestParseAliasTable(t *testing.T) {
	t.Parallel()
	in := []byte(`# alpine aliases
[aliases]
"base-devel" = "build-base"
"python" = "python3"
"perl" = "perl"
ncurses = "ncurses-dev"
`)
	table, err := parseAliasTable(in)
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	cases := map[string]string{
		"base-devel": "build-base",
		"python":     "python3",
		"perl":       "perl",
		"ncurses":    "ncurses-dev",
	}
	for k, want := range cases {
		got, ok := table[k]
		if !ok {
			t.Fatalf("alias %q missing from table", k)
		}
		if got != want {
			t.Fatalf("alias %q = %q, want %q", k, got, want)
		}
	}
}

// TestInstallRejectsEmptyPackages confirms Install nil/empty short-
// circuits before the apk binary lookup so a hostile environment
// (e.g. CI without apk-tools installed) never sees an spurious "no apk
// binary" error from the no-op path.
func TestInstallRejectsEmptyPackages(t *testing.T) {
	cases := []struct {
		name string
		in   []string
	}{
		{"nil", nil},
		{"empty", []string{}},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			if err := Install("/nonexistent-root-for-test", tc.in); err != nil {
				t.Fatalf("Install with %s packages returned error: %v", tc.name, err)
			}
		})
	}
}

// TestInstallLocalRejectsEmptyFiles is the sibling check for the local
// .apk path.
func TestInstallLocalRejectsEmptyFiles(t *testing.T) {
	cases := []struct {
		name string
		in   []string
	}{
		{"nil", nil},
		{"empty", []string{}},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			if err := InstallLocal("/nonexistent-root-for-test", tc.in); err != nil {
				t.Fatalf("InstallLocal with %s packages returned error: %v", tc.name, err)
			}
		})
	}
}

// TestCheckHostPrereqsMissingAPK stubs $PATH so none of `apk`,
// `apk.static`, or `apk-tools-static` resolve. The error must surface
// all three candidates so the user knows what to install regardless of
// which package layout their host distro ships.
func TestCheckHostPrereqsMissingAPK(t *testing.T) {
	old := os.Getenv("PATH")
	t.Cleanup(func() { os.Setenv("PATH", old) })
	if err := os.Setenv("PATH", ""); err != nil {
		t.Fatalf("setenv PATH=\"\": %v", err)
	}
	err := checkHostPrereqs()
	if err == nil {
		t.Fatalf("checkHostPrereqs with empty PATH should fail")
	}
	msg := err.Error()
	for _, want := range []string{"apk", "apk.static", "apk-tools-static", "Alpine", "Arch", "Debian"} {
		if !strings.Contains(msg, want) {
			t.Errorf("error missing %q substring; got: %s", want, msg)
		}
	}
}

func TestFindAPKMissingErrorMentionsCandidates(t *testing.T) {
	// We can't reliably hide apk binaries on every host, so the most
	// useful invariant we can check without a sandbox is that the
	// error string findAPK *would* return mentions all three candidate
	// names + install hints. This guards regressions in the message.
	candidates := []string{"apk", "apk.static", "apk-tools-static"}
	err := errMissingAPK(candidates)
	msg := err.Error()
	for _, name := range candidates {
		if !strings.Contains(msg, name) {
			t.Fatalf("error message missing candidate %q: %s", name, msg)
		}
	}
	for _, hint := range []string{"Alpine", "Arch", "Debian"} {
		if !strings.Contains(msg, hint) {
			t.Fatalf("error message missing distro hint %q: %s", hint, msg)
		}
	}
}
