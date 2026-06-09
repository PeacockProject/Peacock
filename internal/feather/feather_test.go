package feather

import (
	"reflect"
	"strings"
	"testing"
)

// stubLookupBinary swaps lookupBinary with a controllable fake and
// returns a restore func via t.Cleanup. found == false models "ftr not
// installed".
func stubLookupBinary(t *testing.T, path string, found bool) {
	t.Helper()
	old := lookupBinary
	lookupBinary = func() (string, bool) { return path, found }
	t.Cleanup(func() { lookupBinary = old })
}

func TestAvailableFalseWhenBinaryMissing(t *testing.T) {
	stubLookupBinary(t, "", false)
	if Available() {
		t.Fatalf("Available() = true with stubbed missing binary, want false")
	}
}

func TestAvailableTrueWhenBinaryPresent(t *testing.T) {
	stubLookupBinary(t, "/peacock/bin/ftr", true)
	if !Available() {
		t.Fatalf("Available() = false with stubbed present binary, want true")
	}
}

// TestInstallErrorIsVerbatim guards the canonical error string the
// build pipeline currently soft-matches on to decide whether to skip
// the feather step in phase 3.
func TestInstallErrorIsVerbatim(t *testing.T) {
	stubLookupBinary(t, "", false)
	err := Install("settings", InstallOpts{})
	if err == nil {
		t.Fatalf("Install() with missing binary returned nil, want error")
	}
	const want = "ftr binary not found — feather not installed (phase 4 will populate)"
	if err.Error() != want {
		t.Fatalf("Install error mismatch.\n got: %q\nwant: %q", err.Error(), want)
	}
}

// TestBuildArgsGolden walks every InstallOpts shape the build pipeline
// constructs today + a few combinations the future overlay-install
// path will need, and pins the resulting argv slice.
func TestBuildArgsGolden(t *testing.T) {
	t.Parallel()
	cases := []struct {
		name string
		opts InstallOpts
		sub  string
		tail []string
		want []string
	}{
		{
			name: "bare install no opts",
			opts: InstallOpts{},
			sub:  "install",
			tail: []string{"settings"},
			want: []string{"install", "settings"},
		},
		{
			name: "peacock prefix only",
			opts: InstallOpts{PeacockPrefix: "/staging/peacock"},
			sub:  "install",
			tail: []string{"weston"},
			want: []string{"install", "--peacock-prefix", "/staging/peacock", "weston"},
		},
		{
			name: "all prefixes set",
			opts: InstallOpts{
				PeacockPrefix: "/p", AppsPrefix: "/a",
				CompatPrefix: "/c", DataPrefix: "/d",
			},
			sub:  "install",
			tail: []string{"pkg"},
			want: []string{
				"install",
				"--peacock-prefix", "/p",
				"--apps-prefix", "/a",
				"--compat-prefix", "/c",
				"--data-prefix", "/d",
				"pkg",
			},
		},
		{
			name: "extra args interleave before tail",
			opts: InstallOpts{
				PeacockPrefix: "/p",
				ExtraArgs:     []string{"--force", "--no-verify"},
			},
			sub:  "install",
			tail: []string{"pkg"},
			want: []string{
				"install",
				"--peacock-prefix", "/p",
				"--force", "--no-verify",
				"pkg",
			},
		},
		{
			name: "remove subcommand has no prefix flags by default",
			opts: InstallOpts{},
			sub:  "remove",
			tail: []string{"pkg"},
			want: []string{"remove", "pkg"},
		},
		{
			name: "apps prefix only",
			opts: InstallOpts{AppsPrefix: "/apps"},
			sub:  "install",
			tail: []string{"pkg"},
			want: []string{"install", "--apps-prefix", "/apps", "pkg"},
		},
		{
			name: "compat prefix only",
			opts: InstallOpts{CompatPrefix: "/compat"},
			sub:  "install",
			tail: []string{"pkg"},
			want: []string{"install", "--compat-prefix", "/compat", "pkg"},
		},
		{
			name: "data prefix only",
			opts: InstallOpts{DataPrefix: "/data"},
			sub:  "install",
			tail: []string{"pkg"},
			want: []string{"install", "--data-prefix", "/data", "pkg"},
		},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			got := buildArgs(tc.opts, tc.sub, tc.tail...)
			if !reflect.DeepEqual(got, tc.want) {
				t.Fatalf("buildArgs mismatch\n got: %v\nwant: %v", got, tc.want)
			}
		})
	}
}

// TestResolveBinaryReturnsStubPath validates the lookup shim swaps
// cleanly into resolveBinary so the rest of the package wires up to
// it.
func TestResolveBinaryReturnsStubPath(t *testing.T) {
	stubLookupBinary(t, "/tmp/fake-ftr", true)
	got, err := resolveBinary()
	if err != nil {
		t.Fatalf("resolveBinary returned error: %v", err)
	}
	if got != "/tmp/fake-ftr" {
		t.Fatalf("resolveBinary = %q, want %q", got, "/tmp/fake-ftr")
	}
}

// TestResolveBinaryMissingErrorMessage doubles as documentation of
// the soft-signal error string callers downstream pattern-match on.
func TestResolveBinaryMissingErrorMessage(t *testing.T) {
	stubLookupBinary(t, "", false)
	_, err := resolveBinary()
	if err == nil {
		t.Fatalf("resolveBinary with stubbed missing binary returned nil err")
	}
	if !strings.Contains(err.Error(), "ftr binary not found") {
		t.Fatalf("error missing canonical substring; got: %q", err.Error())
	}
}
