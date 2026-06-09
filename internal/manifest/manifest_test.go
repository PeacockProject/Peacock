package manifest

import (
	"path/filepath"
	"testing"
)

// TestResolvedLayout pins the layout-defaulting contract used by every
// downstream Resolved* accessor: explicit values pass through verbatim
// and an empty Install table maps onto "system" so the 51 legacy ports
// keep working without an [install] section.
func TestResolvedLayout(t *testing.T) {
	t.Parallel()

	cases := []struct {
		name   string
		layout string
		want   string
	}{
		{"explicit peacock", "peacock", "peacock"},
		{"explicit app", "app", "app"},
		{"explicit compat", "compat", "compat"},
		{"explicit system", "system", "system"},
		{"empty defaults to system", "", "system"},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			p := &Package{}
			p.Install.Layout = tc.layout
			got := p.ResolvedLayout()
			if got != tc.want {
				t.Fatalf("ResolvedLayout(layout=%q) = %q, want %q", tc.layout, got, tc.want)
			}
		})
	}
}

// TestResolvedLayoutNilReceiver guards the nil-safety branch: legacy
// call sites pass *Package values they haven't validated, and the
// accessor must not panic on nil.
func TestResolvedLayoutNilReceiver(t *testing.T) {
	t.Parallel()
	var p *Package
	if got := p.ResolvedLayout(); got != "system" {
		t.Fatalf("nil.ResolvedLayout() = %q, want %q", got, "system")
	}
}

// TestResolvedPrefix tables every layout against both the explicit
// override path and the layout-default path. Per the meta-distro plan:
//
//	system  -> /usr
//	peacock -> /peacock
//	app     -> /apps/<name>
//	compat  -> /compat/<runtime>
func TestResolvedPrefix(t *testing.T) {
	t.Parallel()

	type pkg struct {
		layout  string
		prefix  string
		name    string
		runtime string
	}

	cases := []struct {
		name string
		in   pkg
		want string
	}{
		// Default-prefix paths.
		{"system layout default prefix", pkg{layout: "system", name: "x"}, "/usr"},
		{"empty layout default prefix", pkg{name: "x"}, "/usr"},
		{"peacock layout default prefix", pkg{layout: "peacock", name: "x"}, "/peacock"},
		{"app layout default prefix uses name", pkg{layout: "app", name: "settings"}, "/apps/settings"},
		{"compat layout with runtime", pkg{layout: "compat", name: "x", runtime: "glibc"}, "/compat/glibc"},
		{"compat layout without runtime falls back to unknown", pkg{layout: "compat", name: "x"}, "/compat/unknown"},
		// Explicit-prefix paths — every layout should honor the override.
		{"system layout explicit prefix", pkg{layout: "system", name: "x", prefix: "/opt"}, "/opt"},
		{"peacock layout explicit prefix", pkg{layout: "peacock", name: "x", prefix: "/peacock-alt"}, "/peacock-alt"},
		{"app layout explicit prefix", pkg{layout: "app", name: "x", prefix: "/apps/custom"}, "/apps/custom"},
		{"compat layout explicit prefix", pkg{layout: "compat", name: "x", prefix: "/compat/musl"}, "/compat/musl"},
		{"empty layout explicit prefix", pkg{name: "x", prefix: "/srv"}, "/srv"},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			p := &Package{}
			p.Package.Name = tc.in.name
			p.Package.Runtime = tc.in.runtime
			p.Install.Layout = tc.in.layout
			p.Install.Prefix = tc.in.prefix
			got := p.ResolvedPrefix()
			if got != tc.want {
				t.Fatalf("ResolvedPrefix(%+v) = %q, want %q", tc.in, got, tc.want)
			}
		})
	}
}

func TestResolvedPrefixNilReceiver(t *testing.T) {
	t.Parallel()
	var p *Package
	if got := p.ResolvedPrefix(); got != "/usr" {
		t.Fatalf("nil.ResolvedPrefix() = %q, want %q", got, "/usr")
	}
}

// TestSupportsFlavor covers the four shape-of-input cases the build
// driver actually hits when expanding a manifest list against a
// configured flavor.
func TestSupportsFlavor(t *testing.T) {
	t.Parallel()

	cases := []struct {
		name    string
		flavors []string
		query   string
		want    bool
	}{
		{"empty slice supports all (arch)", nil, "arch", true},
		{"empty slice supports all (debian)", nil, "debian", true},
		{"empty slice supports all (alpine)", []string{}, "alpine", true},
		{"single match", []string{"arch"}, "arch", true},
		{"single no match", []string{"arch"}, "debian", false},
		{"multi match first", []string{"arch", "debian"}, "arch", true},
		{"multi match last", []string{"arch", "debian", "alpine"}, "alpine", true},
		{"multi no match", []string{"arch", "debian"}, "alpine", false},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			p := &Package{}
			p.Package.Flavor = tc.flavors
			got := p.SupportsFlavor(tc.query)
			if got != tc.want {
				t.Fatalf("SupportsFlavor(%v, %q) = %v, want %v",
					tc.flavors, tc.query, got, tc.want)
			}
		})
	}
}

func TestSupportsFlavorNilReceiver(t *testing.T) {
	t.Parallel()
	var p *Package
	if !p.SupportsFlavor("arch") {
		t.Fatalf("nil.SupportsFlavor(arch) = false, want true")
	}
}

// TestLoadPackageGoldenFixtures parses every TOML under testdata/ and
// verifies the Resolved* accessors yield the expected shape. This is
// the round-trip safety net: schema accessors landed in 219dca7 will
// stay correct as long as these tiny fixtures keep parsing into the
// same Package values.
func TestLoadPackageGoldenFixtures(t *testing.T) {
	t.Parallel()

	cases := []struct {
		file        string
		wantLayout  string
		wantPrefix  string
		wantFlavors []string
	}{
		{
			file:       "system_default.toml",
			wantLayout: "system",
			wantPrefix: "/usr",
		},
		{
			file:       "layout_peacock.toml",
			wantLayout: "peacock",
			wantPrefix: "/peacock",
		},
		{
			file:        "layout_app_compat_runtime.toml",
			wantLayout:  "compat",
			wantPrefix:  "/compat/glibc",
			wantFlavors: nil,
		},
		{
			file:        "flavors_arch_debian.toml",
			wantLayout:  "system",
			wantPrefix:  "/usr",
			wantFlavors: []string{"arch", "debian"},
		},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.file, func(t *testing.T) {
			t.Parallel()
			path := filepath.Join("testdata", tc.file)
			pkg, err := LoadPackage(path)
			if err != nil {
				t.Fatalf("LoadPackage(%s): %v", path, err)
			}
			if got := pkg.ResolvedLayout(); got != tc.wantLayout {
				t.Errorf("layout = %q, want %q", got, tc.wantLayout)
			}
			if got := pkg.ResolvedPrefix(); got != tc.wantPrefix {
				t.Errorf("prefix = %q, want %q", got, tc.wantPrefix)
			}
			if len(tc.wantFlavors) > 0 {
				for _, f := range tc.wantFlavors {
					if !pkg.SupportsFlavor(f) {
						t.Errorf("SupportsFlavor(%q) = false, want true", f)
					}
				}
			}
		})
	}
}
