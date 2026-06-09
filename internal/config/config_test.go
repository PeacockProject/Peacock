package config

import (
	"testing"

	"github.com/spf13/viper"
)

// resetViper wipes the global viper state between sub-tests so each one
// is hermetic. viper has no public Reset(), so we lean on the public
// surface: New() returns a fresh instance and Set/Get on the package-
// level functions delegate to the singleton, which we swap in
// repeatedly via SetDefault clears. The simplest hermetic approach is
// to call viper.Reset() — it's exported in modern viper and resets the
// global singleton.
func resetViper() {
	viper.Reset()
}

// TestIsValidFlavor pins the public flavor allowlist. Changing this
// list is a user-facing CLI change and downstream tests should fail
// loud if "arch"/"debian"/"alpine" stop being valid or unexpected
// values start being accepted.
func TestIsValidFlavor(t *testing.T) {
	cases := []struct {
		in   string
		want bool
	}{
		{"arch", true},
		{"debian", true},
		{"alpine", true},
		{"foo", false},
		{"", false},
		{"ARCH", false},    // case-sensitive on purpose
		{"arch ", false},   // exact match only
		{"archive", false}, // not a prefix match
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.in, func(t *testing.T) {
			got := IsValidFlavor(tc.in)
			if got != tc.want {
				t.Fatalf("IsValidFlavor(%q) = %v, want %v", tc.in, got, tc.want)
			}
		})
	}
}

// TestFlavorDefault covers the migration safety net: legacy configs
// written before KeyFlavor existed must continue to target Arch.
func TestFlavorDefault(t *testing.T) {
	resetViper()
	if got := Flavor(); got != "arch" {
		t.Fatalf("Flavor() with unset key = %q, want %q", got, "arch")
	}
}

// TestFlavorExplicit verifies the accessor returns whatever the caller
// stored under KeyFlavor — including values that aren't in
// ValidFlavors. Validation is the CLI layer's job; the accessor only
// transports the value.
func TestFlavorExplicit(t *testing.T) {
	cases := []string{"arch", "debian", "alpine", "custom-flavor"}
	for _, want := range cases {
		want := want
		t.Run(want, func(t *testing.T) {
			resetViper()
			viper.Set(KeyFlavor, want)
			if got := Flavor(); got != want {
				t.Fatalf("Flavor() = %q, want %q", got, want)
			}
		})
	}
}

// TestKeyConstants is a regression guard: these strings end up as JSON
// keys in users' on-disk config.json. Renaming a constant silently
// would mean an existing user's setting disappears after upgrade. If
// any of these mismatches the literal string, the test fails loudly
// and the offender either ships a migration or reverts the rename.
func TestKeyConstants(t *testing.T) {
	cases := []struct {
		name string
		got  string
		want string
	}{
		{"KeyWorkDir", KeyWorkDir, "work_dir"},
		{"KeyInitSystem", KeyInitSystem, "init_system"},
		{"KeyDesktop", KeyDesktop, "desktop"},
		{"KeyDisplayManager", KeyDisplayManager, "display_manager"},
		{"KeyExtraPackages", KeyExtraPackages, "extra_packages"},
		{"KeyUserName", KeyUserName, "user_name"},
		{"KeyUserPassword", KeyUserPassword, "user_password"},
		{"KeyEmptyRootfs", KeyEmptyRootfs, "empty_rootfs"},
		{"KeyImageSizeMB", KeyImageSizeMB, "image_size_mb"},
		{"KeyFlavor", KeyFlavor, "flavor"},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			if tc.got == "" {
				t.Fatalf("%s is empty string", tc.name)
			}
			if tc.got != tc.want {
				t.Fatalf("%s = %q, want %q (rename will break existing user configs)",
					tc.name, tc.got, tc.want)
			}
		})
	}
}

// TestValidFlavorsContents pins the list contents so a downstream
// "remove arch" change has to update this test too.
func TestValidFlavorsContents(t *testing.T) {
	want := []string{"arch", "debian", "alpine"}
	if len(ValidFlavors) != len(want) {
		t.Fatalf("ValidFlavors len = %d, want %d (%v)", len(ValidFlavors), len(want), want)
	}
	for i, v := range want {
		if ValidFlavors[i] != v {
			t.Fatalf("ValidFlavors[%d] = %q, want %q", i, ValidFlavors[i], v)
		}
	}
}
