package apk

import (
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
