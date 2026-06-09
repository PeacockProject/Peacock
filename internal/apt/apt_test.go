package apt

import (
	"os"
	"strings"
	"testing"
)

func TestArchToDpkg(t *testing.T) {
	tests := []struct {
		name    string
		in      string
		want    string
		wantErr bool
	}{
		{"aarch64 -> arm64", "aarch64", "arm64", false},
		{"arm64 -> arm64", "arm64", "arm64", false},
		{"armv7h -> armhf", "armv7h", "armhf", false},
		{"armv7 -> armhf", "armv7", "armhf", false},
		{"armhf -> armhf", "armhf", "armhf", false},
		{"x86_64 -> amd64", "x86_64", "amd64", false},
		{"amd64 -> amd64", "amd64", "amd64", false},
		{"empty -> err", "", "", true},
		{"unknown -> err", "riscv64", "", true},
		{"junk -> err", "potato", "", true},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := ArchToDpkg(tc.in)
			if tc.wantErr {
				if err == nil {
					t.Fatalf("ArchToDpkg(%q) = %q, want error", tc.in, got)
				}
				return
			}
			if err != nil {
				t.Fatalf("ArchToDpkg(%q) returned error: %v", tc.in, err)
			}
			if got != tc.want {
				t.Errorf("ArchToDpkg(%q) = %q, want %q", tc.in, got, tc.want)
			}
		})
	}
}

func TestGenerateConfigContentBookworm(t *testing.T) {
	cfg := Config{Suite: Bookworm, Arch: "arm64", Mirror: DefaultMirror}
	got := GenerateConfigContent(cfg)
	want := strings.Join([]string{
		"deb http://deb.debian.org/debian bookworm main",
		"deb http://deb.debian.org/debian bookworm-updates main",
		"deb http://security.debian.org/debian-security bookworm-security main",
		"",
	}, "\n")
	if got != want {
		t.Errorf("GenerateConfigContent(bookworm) mismatch.\n got: %q\nwant: %q", got, want)
	}
}

func TestGenerateConfigContentSidSkipsSecurity(t *testing.T) {
	cfg := Config{Suite: Sid, Arch: "amd64", Mirror: DefaultMirror}
	got := GenerateConfigContent(cfg)
	want := strings.Join([]string{
		"deb http://deb.debian.org/debian sid main",
		"deb http://deb.debian.org/debian sid-updates main",
		"",
	}, "\n")
	if got != want {
		t.Errorf("GenerateConfigContent(sid) mismatch.\n got: %q\nwant: %q", got, want)
	}
}

func TestGenerateConfigContentDefaults(t *testing.T) {
	// Empty Suite/Mirror should default to bookworm + DefaultMirror.
	got := GenerateConfigContent(Config{Arch: "arm64"})
	if !strings.Contains(got, "bookworm main") {
		t.Errorf("expected default suite bookworm in output, got: %q", got)
	}
	if !strings.Contains(got, DefaultMirror) {
		t.Errorf("expected default mirror %q in output, got: %q", DefaultMirror, got)
	}
}

func TestGenerateConfigContentExtraComponents(t *testing.T) {
	cfg := Config{
		Suite:           Trixie,
		Arch:            "arm64",
		Mirror:          DefaultMirror,
		ExtraComponents: []string{"contrib", "non-free"},
	}
	got := GenerateConfigContent(cfg)
	if !strings.Contains(got, "trixie main contrib non-free") {
		t.Errorf("expected trixie main contrib non-free in output, got: %q", got)
	}
	if !strings.Contains(got, "trixie-security main contrib non-free") {
		t.Errorf("expected trixie-security with components in output, got: %q", got)
	}
}

// TestInstallRejectsEmptyPackages confirms Install short-circuits on
// nil / empty input before shelling out. Sub-tests run against both
// shapes so the contract is unambiguous.
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
			// "/nonexistent" guarantees that if Install actually tries
			// to exec apt-get against it we'd surface a real error
			// instead of the short-circuit nil.
			if err := Install("/nonexistent-root-for-test", tc.in); err != nil {
				t.Fatalf("Install with %s packages returned error: %v", tc.name, err)
			}
		})
	}
}

// TestCheckHostPrereqsMissingDebootstrap stubs $PATH so debootstrap
// can never resolve and asserts checkHostPrereqs surfaces an actionable
// error mentioning the binary name and install hints.
func TestCheckHostPrereqsMissingDebootstrap(t *testing.T) {
	old := os.Getenv("PATH")
	t.Cleanup(func() { os.Setenv("PATH", old) })
	if err := os.Setenv("PATH", ""); err != nil {
		t.Fatalf("setenv PATH=\"\": %v", err)
	}
	err := checkHostPrereqs(Config{Arch: "arm64"})
	if err == nil {
		t.Fatalf("checkHostPrereqs with empty PATH should fail")
	}
	msg := err.Error()
	for _, want := range []string{"debootstrap", "Debian/Ubuntu", "Arch Linux"} {
		if !strings.Contains(msg, want) {
			t.Errorf("error missing %q substring; got: %s", want, msg)
		}
	}
}

// TestCheckHostPrereqsMissingQemuStatic uses a non-native target arch
// to force the qemu-user-static check, then stubs $PATH to fail. The
// resulting error must mention the specific qemu binary the build
// needs.
func TestCheckHostPrereqsMissingQemuStatic(t *testing.T) {
	old := os.Getenv("PATH")
	t.Cleanup(func() { os.Setenv("PATH", old) })
	if err := os.Setenv("PATH", ""); err != nil {
		t.Fatalf("setenv PATH=\"\": %v", err)
	}
	// arm64 target — qemuStaticBinaryForArch returns
	// qemu-aarch64-static on a non-arm64 host. Even on an arm64 host
	// the empty PATH still hits debootstrap first, so we'd see the
	// debootstrap message; this test runs identically on every host
	// because the actionable error always mentions either debootstrap
	// or the qemu binary by name.
	err := checkHostPrereqs(Config{Arch: "arm64"})
	if err == nil {
		t.Fatalf("checkHostPrereqs(empty PATH) should fail")
	}
	msg := err.Error()
	// One of the two prereqs missed must be named.
	if !strings.Contains(msg, "debootstrap") && !strings.Contains(msg, "qemu-aarch64-static") {
		t.Fatalf("expected debootstrap or qemu-aarch64-static in error, got: %s", msg)
	}
}
